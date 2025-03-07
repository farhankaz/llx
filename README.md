# LLX - Your AI-Powered Terminal Assistant for MacOS

`llx` is a MacOs CLI tool that assists with terminal commands, shell scripting, and development tasks. Leveraging llama.cpp with Metal GPU acceleration, it provides instant help with Unix commands, script generation, and common development workflows - all without leaving your terminal. The tool automatically downloads and uses the Granite-3.1 2B Instruct model by default, optimized for instruction following and command generation.

### System Requirements

- Apple Silicon Mac (M1/M2/M3/M4)
- macOS Ventura or later
- ~4GB disk space for the default model

## Installation

Install `llx` using Homebrew:
```bash
brew tap farhankaz/tap
brew install llx
```

 The tool will automatically download the default Granite-3.1 2B Instruct model on first use.

### Usage

Basic usage for system tasks:
```bash
llx "Find all Python files containing the word 'deprecated' in the current directory"
# Outputs: find . -name "*.py" -type f -exec grep -l "deprecated" {} \;

llx "Show disk usage of subdirectories in /usr, sorted by size"
# Outputs: du -h /usr --max-depth=1 | sort -hr

llx "Kill all processes using port 8080"
# Outputs: lsof -ti:8080 | xargs kill -9

llx "Create a script to backup all MySQL databases and compress them" > backup-dbs.sh
# Generates a complete backup script with error handling and logging

llx "Monitor system load and alert if CPU usage exceeds 90%" 
Outputs: ```bash
while true; do
  cpu_load=$(top -bn1 | grep "Cpu(s)" | sed "s/.*, *\([0-9.]*\)%* id.*/\1/" | awk '{print 100 - $1}')
  if [ "$cpu_load" -gt 90 ]; then
    echo "$(date '+%Y-%m-%d %H:%M:%S'): CPU usage is at $(bc -l <<< "90 + ($cpu_load / 2)")%" > alert.log
    mail -s "CPU Usage Alert" user@example.com < alert.log
  fi
done

# Show version
llx --version

# Shutdown the daemon, normally auto starts when needed
llx --shutdown
```

### Daemon Management

`llx` automatically starts its daemon (`llxd`) in the background when needed. The daemon manages the LLM model and handles inference requests. By default, it will download and use the Granite-3.1 2B Instruct model, which is optimized for command generation and system tasks.

To use a custom model, you can start the daemon manually with:
```bash
llxd -m /path/to/your/model.gguf
```

The daemon can be stopped gracefully using:
```bash
llx --shutdown
```

### Model Download Path
By default the Granite-3.1 2B model is downloaded from Huggingface to the $HOME/.cache/llx/models directory.  If you wish to store the model in an alternate location, maybe to use an external SSD for large model files, you can change the location by setting the `LLX_MODELS_DIR` environment variable to an alternate path.

## License

MIT: https://opensource.org/license/mit

