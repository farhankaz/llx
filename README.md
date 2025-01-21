# LLX - Local Inference Terminal Command For Apple Silicone Macs

`llx` is a utlity for Apple Silicone Macs for fast LLM text generation in the terminal. `llx` uses llama.cpp with native Metal GPU acceleration, offering low-latency command and script generation without leaving your terminal.  Downloads and uses llama 3.2 3B model by default, but another model can be used.

### Usage

Make sure the `llxd` daemon is started (see below).Basic usage for system tasks:
```bash
llx "Find all Python files containing the word 'deprecated' in the current directory"
# Outputs: find . -name "*.py" -type f -exec grep -l "deprecated" {} \;

llx "Show disk usage of subdirectories in /usr, sorted by size"
# Outputs: du -h /usr --max-depth=1 | sort -hr

llx "Kill all processes using port 8080"
# Outputs: lsof -ti:8080 | xargs kill -9
```

### Advanced Usage

Chain commands with pipes:
```bash
ps aux | llx "Find processes using more than 1GB of memory"
# Generates and executes: awk '$6 > 1000000 {print}'
```

Generate complex scripts:
```bash
llx "Create a script to backup all MySQL databases and compress them" > backup-dbs.sh
# Generates a complete backup script with error handling and logging
```

Use with system monitoring:
```bash
llx "Monitor system load and alert if CPU usage exceeds 90%" 
# Generates and executes appropriate monitoring commands
```

### Starting the Daemon (llxd)

`llx` needs the `llxd` daemon to be running with a model loaded in memory. To start `llxd` with the default model:
```bash
llxd
```
This will automatically download and use the Llama 3.2 3B parameter model.

Using a custom model:
```bash
llxd -m /path/to/your/model.gguf
```
