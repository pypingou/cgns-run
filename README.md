# cgns-run - Namespace and Cgroup Entry Tool

A C program that executes a command in the same namespaces and cgroups as an existing process.

## Features

- **List mode**: Display namespace and cgroup information for any process
- **Execute mode**: Join all available Linux namespaces (mount, uts, ipc, pid, net, user, cgroup)
- Migrates to the same cgroups as the target process
- Executes the specified command with all arguments
- Robust error handling and validation
- Easy comparison of namespace/cgroup contexts between processes

## Differences from nsenter

While similar to the standard Linux `nsenter` utility, `cgns-run` has some key differences:

- **Automatic cgroup migration**: Unlike `nsenter`, this tool automatically migrates the new process to the same cgroups as the target process
- **Simplified interface**: Takes only a PID and command - no need to specify individual namespace flags
- **All-or-nothing namespaces**: Attempts to join all available namespaces from the target process automatically
- **Cgroup-aware**: Specifically designed to handle both namespaces and cgroups in container environments

The standard `nsenter` requires you to specify which namespaces to enter with flags like `-m`, `-u`, `-i`, etc., and doesn't handle cgroup migration.

## Building

```bash
make
```

## Usage

```bash
./cgns-run [OPTIONS] <target_pid> [command] [args...]
```

**Options:**
- `-l, --list`: List namespaces and cgroups info for the target PID
- `-d, --diff`: Compare namespaces and cgroups between two PIDs
- `-h, --help`: Show help message

**Note:** This program typically requires root privileges to join most namespaces.

## Examples

```bash
# List namespace and cgroup info for process 1234
./cgns-run -l 1234

# Compare two processes to see if they share namespaces/cgroups
./cgns-run -d 1234 5678

# Run 'ps aux' in the same namespaces as process 1234
sudo ./cgns-run 1234 ps aux

# Start a shell in the same environment as process 5678
sudo ./cgns-run 5678 /bin/bash

# Run a command with arguments
sudo ./cgns-run 1234 ls -la /proc/self/ns/
```

## How it works

1. **Namespace joining**: Opens `/proc/PID/ns/*` files and uses `setns()` system call
2. **Cgroup migration**: Reads `/proc/PID/cgroup` and writes current PID to target cgroup files
3. **Command execution**: Uses `execvp()` to replace current process with the specified command

## Security Considerations

- Requires appropriate privileges (typically root)
- Validates target PID exists before attempting operations
- Continues execution even if some cgroups cannot be joined (non-fatal)

## Cleaning up

```bash
make clean
```

## License

MIT License - see [LICENSE](LICENSE) file for details.