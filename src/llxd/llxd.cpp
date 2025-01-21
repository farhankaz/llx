#include "llxd.h"
#include "llama.h"
#include "common/sampling.h"
#include "common/common.h"
#include "llama-chat.h"
#include "prompts.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <errno.h>
#include <sstream>
#include <iomanip>

#define DEBUG_LOG(x) if (debug_mode_) { std::cout << "[DEBUG] " << x << std::endl; }

// Request structure to hold client request data
struct Request {
    int client_fd;
    std::string prompt;
};

// Metrics structure to track performance
struct Metrics {
    int64_t t_start = 0;

    // Total metrics since daemon start
    uint64_t n_prompt_tokens_processed_total = 0;
    uint64_t t_prompt_processing_total = 0;      // ms
    uint64_t n_tokens_predicted_total = 0;
    uint64_t t_tokens_generation_total = 0;      // ms

    // Current request metrics
    uint64_t n_prompt_tokens_processed = 0;
    uint64_t t_prompt_processing = 0;            // ms
    uint64_t n_tokens_predicted = 0;
    uint64_t t_tokens_generation = 0;            // ms

    // Stats
    uint64_t n_requests_processed = 0;
    uint64_t n_active_requests = 0;

    void init() {
        t_start = ggml_time_us();
    }

    void on_prompt_eval(int n_tokens, int64_t t_start_us, int64_t t_end_us) {
        n_prompt_tokens_processed += n_tokens;
        n_prompt_tokens_processed_total += n_tokens;
        
        double t_ms = (t_end_us - t_start_us) / 1e3;
        t_prompt_processing += t_ms;
        t_prompt_processing_total += t_ms;
    }

    void on_token_generated(int64_t t_start_us, int64_t t_end_us) {
        n_tokens_predicted++;
        n_tokens_predicted_total++;

        double t_ms = (t_end_us - t_start_us) / 1e3;
        t_tokens_generation += t_ms;
        t_tokens_generation_total += t_ms;
    }

    void on_request_start() {
        n_active_requests++;
        n_requests_processed++;
        
        // Reset per-request counters
        n_prompt_tokens_processed = 0;
        t_prompt_processing = 0;
        n_tokens_predicted = 0;
        t_tokens_generation = 0;
    }

    void on_request_end() {
        n_active_requests--;
        
        // Log metrics for this request
        if (n_tokens_predicted > 0) {
            double prompt_tokens_per_sec = n_prompt_tokens_processed / (t_prompt_processing / 1e3);
            double gen_tokens_per_sec = n_tokens_predicted / (t_tokens_generation / 1e3);
            
            std::cout << "\nRequest Metrics:" << std::endl;
            std::cout << "Prompt processing: " << n_prompt_tokens_processed << " tokens, "
                      << t_prompt_processing << " ms (" << prompt_tokens_per_sec << " tokens/sec)" << std::endl;
            std::cout << "Token generation: " << n_tokens_predicted << " tokens, "
                      << t_tokens_generation << " ms (" << gen_tokens_per_sec << " tokens/sec)" << std::endl;
        }

        // Log total metrics periodically
        if (n_requests_processed % 10 == 0) {
            double total_prompt_tokens_per_sec = n_prompt_tokens_processed_total / (t_prompt_processing_total / 1e3);
            double total_gen_tokens_per_sec = n_tokens_predicted_total / (t_tokens_generation_total / 1e3);
            
            std::cout << "\nTotal Metrics:" << std::endl;
            std::cout << "Total requests processed: " << n_requests_processed << std::endl;
            std::cout << "Total prompt tokens: " << n_prompt_tokens_processed_total 
                      << " (" << total_prompt_tokens_per_sec << " tokens/sec)" << std::endl;
            std::cout << "Total generated tokens: " << n_tokens_predicted_total
                      << " (" << total_gen_tokens_per_sec << " tokens/sec)" << std::endl;
        }
    }
};

class llxd::Impl {
public:
    Impl(const std::string& model_path, bool debug_mode) 
        : model_path_(model_path)
        , running_(false)
        , debug_mode_(debug_mode)
        , socket_fd_(-1)
        , model_(nullptr) {
        metrics_.init();
        DEBUG_LOG("Initializing daemon with model: " << model_path);
    }

    bool start() {
        DEBUG_LOG("Starting daemon initialization");
        
        // Initialize llama.cpp
        llama_backend_init();
        DEBUG_LOG("Initialized llama backend");

        // Load the model with optimized parameters for Apple Silicon
        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = 99;
        model_params.main_gpu = 0;
        model_params.tensor_split = nullptr;
        model_params.use_mmap = true;
        model_params.use_mlock = false;
        
        DEBUG_LOG("Loading model with params:"
                 "\n  n_gpu_layers: " << model_params.n_gpu_layers <<
                 "\n  use_mmap: " << model_params.use_mmap <<
                 "\n  use_mlock: " << model_params.use_mlock);
        
        model_ = llama_model_load_from_file(model_path_.c_str(), model_params);
        if (!model_) {
            std::cerr << "Failed to load model: " << model_path_ << std::endl;
            return false;
        }

        // Create Unix domain socket
        socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, "/tmp/llx.sock", sizeof(addr.sun_path) - 1);

        // Remove existing socket file if it exists
        unlink(addr.sun_path);

        if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Failed to bind socket" << std::endl;
            return false;
        }

        if (listen(socket_fd_, 5) < 0) {
            std::cerr << "Failed to listen on socket" << std::endl;
            return false;
        }

        running_ = true;
        DEBUG_LOG("Starting worker and accept threads");
        
        // Start worker thread to process requests
        worker_thread_ = std::thread(&Impl::process_requests, this);
        
        // Start accept thread to handle incoming connections
        accept_thread_ = std::thread(&Impl::accept_connections, this);

        return true;
    }

    void stop() {
        running_ = false;
        
        // Wake up worker thread
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_condition_.notify_one();
        }
        
        if (socket_fd_ >= 0) {
            close(socket_fd_);
        }
        
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
        
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        
        if (model_) {
            llama_model_free(model_);
        }
        
        llama_backend_free();
    }

private:
    void accept_connections() {
        while (running_) {
            int client_fd = accept(socket_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (running_) {
                    std::cerr << "Failed to accept connection" << std::endl;
                }
                continue;
            }

            // Read the prompt
            char buffer[4096];
            ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n <= 0) {
                std::cerr << "Failed to read prompt from client" << std::endl;
                close(client_fd);
                continue;
            }
            buffer[n] = '\0';

            // Queue the request
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                request_queue_.push({client_fd, std::string(buffer)});
                queue_condition_.notify_one();
            }
        }
    }

    void process_requests() {
        while (running_) {
            Request request;
            
            // Wait for and get next request
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_condition_.wait(lock, [this] {
                    return !request_queue_.empty() || !running_;
                });
                
                if (!running_) {
                    break;
                }
                
                request = request_queue_.front();
                request_queue_.pop();
            }

            handle_request(request);
        }
    }

        void handle_request(const Request& request) {
        metrics_.on_request_start();
        int64_t t_start_prompt = ggml_time_us();

        // Use RAII for client socket
        struct ClientSocket {
            int fd;
            ClientSocket(int fd) : fd(fd) {}
            ~ClientSocket() { 
                if (fd >= 0) {
                    close(fd);
                }
            }
        } client_guard(request.client_fd);

        // Create optimized context parameters for this request
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 2048;      // Keep context size reasonable
        ctx_params.n_batch = 512;     // Increase batch size for better throughput
        ctx_params.n_threads = 8;     // Optimize for M1/M2 performance
        ctx_params.n_threads_batch = 8;// Match batch threads to CPU cores
        ctx_params.offload_kqv = true;// Enable KQV offloading to GPU
        
        // Create context for this request
        llama_context* ctx = llama_init_from_model(model_, ctx_params);
        if (!ctx) {
            std::cerr << "Failed to create context for request" << std::endl;
            metrics_.on_request_end();
            return;
        }
        DEBUG_LOG("Created context successfully");

        // Detect chat template
        std::string model_template;
        const char* raw_template = llama_model_chat_template(model_);
        if (raw_template != nullptr) {
            model_template = raw_template;
        }
        
        // Default to ChatML if no template or unknown
        llm_chat_template chat_template = LLM_CHAT_TEMPLATE_LLAMA_3;
        if (!model_template.empty()) {
            try {
                chat_template = llm_chat_detect_template(model_template);
                if (chat_template == LLM_CHAT_TEMPLATE_UNKNOWN) {
                    DEBUG_LOG("Unknown chat template, defaulting to ChatML");
                    chat_template = LLM_CHAT_TEMPLATE_LLAMA_3;
                }
            } catch (const std::exception& e) {
                DEBUG_LOG("Error detecting chat template: " << e.what() << ", defaulting to LLama3");
                chat_template = LLM_CHAT_TEMPLATE_LLAMA_3;
            }
        }
        DEBUG_LOG("Using chat template: " << (model_template.empty() ? "LLama3 (default)" : model_template));

        // Create chat messages
        std::vector<llama_chat_message> messages;
        
        // Add system message with more specific instructions
        llama_chat_message system_msg;
        system_msg.role = "system";
        system_msg.content = UNIX_COMMAND_SYSTEM_PROMPT;
        messages.push_back(system_msg);

        // Add user message
        llama_chat_message user_msg;
        user_msg.role = "user";
        user_msg.content = request.prompt.c_str();
        messages.push_back(user_msg);

        // Convert messages to prompt using template
        std::vector<const llama_chat_message*> msg_ptrs;
        for (const auto& msg : messages) {
            msg_ptrs.push_back(&msg);
        }

        // First get required size
        std::string formatted_prompt;
        int prompt_size = llm_chat_apply_template(chat_template, msg_ptrs, formatted_prompt, true);
        if (prompt_size < 0) {
            std::cerr << "Failed to apply chat template (size check)" << std::endl;
            llama_free(ctx);
            metrics_.on_request_end();
            return;
        }

        // Now apply template with proper size
        if (llm_chat_apply_template(chat_template, msg_ptrs, formatted_prompt, true) < 0) {
            std::cerr << "Failed to apply chat template (formatting)" << std::endl;
            llama_free(ctx);
            metrics_.on_request_end();
            return;
        }
        DEBUG_LOG("Applied chat template successfully. Prompt size: " << formatted_prompt.size());
        DEBUG_LOG("Formatted prompt:\n" << formatted_prompt);


        // Get vocab for tokenization
        const llama_vocab* vocab = llama_model_get_vocab(model_);
        if (!vocab) {
            std::cerr << "Failed to get vocab from model" << std::endl;
            llama_free(ctx);
            metrics_.on_request_end();
            return;
        }

        std::string full_prompt = formatted_prompt;

        // Tokenize prompt
        std::vector<llama_token> tokens;
        tokens.resize(full_prompt.length() + 1);
        int n_tokens = llama_tokenize(
            vocab,
            full_prompt.c_str(),
            full_prompt.length(),
            tokens.data(),
            tokens.size(),
            true,  // add_bos
            true   // special tokens
        );
        if (n_tokens < 0) {
            std::cerr << "Failed to tokenize prompt" << std::endl;
            llama_free(ctx);
            metrics_.on_request_end();
            return;
        }
        tokens.resize(n_tokens);
        DEBUG_LOG("Tokenized prompt into " << n_tokens << " tokens");

        // Create batch for prompt evaluation
        llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
        if (!batch.token) {
            std::cerr << "Failed to create batch" << std::endl;
            llama_free(ctx);
            metrics_.on_request_end();
            return;
        }

        // Enable logits for last token
        auto* logits_array = new int8_t[batch.n_tokens]();
        logits_array[batch.n_tokens - 1] = 1;
        batch.logits = logits_array;

        // Evaluate prompt
        if (llama_decode(ctx, batch)) {
            std::cerr << "Failed to evaluate prompt" << std::endl;
            delete[] logits_array;
            llama_free(ctx);
            metrics_.on_request_end();
            return;
        }
        delete[] logits_array;

        int64_t t_end_prompt = ggml_time_us();
        metrics_.on_prompt_eval(n_tokens, t_start_prompt, t_end_prompt);

        // Setup sampling parameters for more precise responses
        common_params_sampling sampling_params;
        sampling_params.temp = 0.2f;          // Lower temperature for more deterministic output
        sampling_params.top_p = 0.1f;         // More focused token selection
        sampling_params.min_p = 0.05f;        // Slightly higher minimum probability
        sampling_params.penalty_repeat = 1.3f; // Stronger repetition penalty
        sampling_params.n_probs = 0;
        sampling_params.penalty_freq = 0.0f;
        sampling_params.penalty_present = 0.0f;
        
        auto* sampler = common_sampler_init(model_, sampling_params);
        if (!sampler) {
            std::cerr << "Failed to initialize sampler" << std::endl;
            llama_free(ctx);
            metrics_.on_request_end();
            return;
        }

        // Generate response with better control
        const int max_tokens = 256;  // Limit maximum tokens since commands should be short
        auto* next_logits = new int8_t[1]{1};
        int64_t t_start_token, t_end_token;
        std::string response;
        bool found_newline = false;

        for (int i = 0; i < max_tokens; i++) {
            t_start_token = ggml_time_us();
            
            // Sample next token
            llama_token new_token = common_sampler_sample(sampler, ctx, -1);

            // Check for end conditions
            if (new_token == llama_vocab_eos(vocab) || 
                llama_vocab_is_eog(vocab, new_token) ||
                (found_newline && (new_token == llama_vocab_bos(vocab) || new_token == llama_vocab_eos(vocab)))) {
                const char* newline = "\n";
                send(request.client_fd, newline, 1, 0);
                break;
            }

            // Convert token to text
            char piece_buf[32];
            int piece_len = llama_token_to_piece(
                vocab,
                new_token,
                piece_buf,
                sizeof(piece_buf),
                0,
                true
            );
            if (piece_len < 0 || piece_len >= (int)sizeof(piece_buf)) {
                break;
            }
            piece_buf[piece_len] = '\0';
            
            // Track if we've seen a newline
            if (strchr(piece_buf, '\n') != nullptr) {
                found_newline = true;
            }

            // Send piece to client
            ssize_t sent = send(request.client_fd, piece_buf, piece_len, MSG_NOSIGNAL);
            if (sent < 0) {
                break;
            }

            // Collect response for logging
            response += std::string(piece_buf, piece_len);

            // Accept token and prepare next batch
            common_sampler_accept(sampler, new_token, true);

            llama_batch next_batch = llama_batch_get_one(&new_token, 1);
            if (!next_batch.token) {
                break;
            }

            next_batch.logits = next_logits;
            static llama_seq_id seq_id = 0;
            static llama_seq_id* seq_id_ptr = &seq_id;
            next_batch.seq_id = &seq_id_ptr;

            if (llama_decode(ctx, next_batch)) {
                break;
            }

            t_end_token = ggml_time_us();
            metrics_.on_token_generated(t_start_token, t_end_token);
        }

        // Log complete response
        DEBUG_LOG("Complete LLM response for request:\n" << response);

        // Cleanup
        delete[] next_logits;
        common_sampler_free(sampler);
        llama_free(ctx);
        metrics_.on_request_end();
    }  // Implementation continues...

    std::string model_path_;
    std::atomic<bool> running_;
    bool debug_mode_;
    int socket_fd_;
    std::thread accept_thread_;
    std::thread worker_thread_;
    llama_model* model_;

    std::queue<Request> request_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    Metrics metrics_;
};

llxd::llxd(const std::string& model_path, bool debug_mode)
    : impl(std::make_unique<Impl>(model_path, debug_mode)) {}

llxd::~llxd() = default;

bool llxd::start() {
    return impl->start();
}

void llxd::stop() {
    impl->stop();
}
