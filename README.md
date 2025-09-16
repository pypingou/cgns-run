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
- `-r, --rootfs`: Automatically prefix command with detected rootfs path (for containers)
- `-D, --debug`: Enable verbose debug output
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

# Run httpd in a container with automatic path translation and debug output
sudo ./cgns-run -D -r 412 /usr/sbin/httpd -DFOREGROUND
```

## How it works

1. **Information gathering**: Pre-reads namespace and cgroup information from `/proc/PID/`
2. **Cgroup migration**: Writes current PID to target cgroup files in `/sys/fs/cgroup/`
3. **Namespace joining**: Opens `/proc/PID/ns/*` files and uses `setns()` system call
4. **Command execution**: Uses `execv()` or `execvp()` to replace current process with the specified command

### Operation Order

The tool performs operations in this specific order to avoid issues with mount namespace changes affecting subsequent operations:

1. **Pre-read target process information** (namespaces, cgroups, rootfs)
2. **Join cgroups first** (before mount namespace changes affect `/sys/fs/cgroup` visibility)
3. **Join namespaces** (mount namespace joined last to preserve host filesystem access)
4. **Execute target command** in the new context

## Understanding Namespace Behavior

### Process Relationships After Execution

When `cgns-run` executes a command, it creates interesting process relationships that are important to understand:

```bash
# Example: Running httpd in container context
sudo ./cgns-run 412 /usr/sbin/httpd -DFOREGROUND
```

This creates a process tree where:
- **Parent process**: Remains in the host namespaces (visible to host `ps`)
- **Child processes**: Inherit the container namespaces

### Namespace Inheritance

When comparing processes with `cgns-run -d`, you may see different PID namespaces between parent and child processes:

```bash
$ cgns-run -d 2354 412  # Parent httpd vs container process
NAMESPACES:
  pid: DIFFERENT
    PID 2354: pid:[4026531836]  # Host PID namespace
    PID 412:  pid:[4026532318]  # Container PID namespace

$ cgns-run -d 2356 412  # Child httpd vs container process
NAMESPACES:
  pid: SAME                   # Both in container PID namespace
```

**Why this happens:**
- The `cgns-run` process joins the container's PID namespace
- When `execv()` is called, the new process inherits these namespaces
- Child processes spawned by the executed command remain in the container namespaces
- The original parent (visible from host) shows the transition point

### Container Path Translation

The `-r/--rootfs` flag enables automatic path translation for container environments:

```bash
# Without -r: May fail if /usr/sbin/httpd doesn't exist on host
sudo ./cgns-run 412 /usr/sbin/httpd

# With -r: Automatically translates paths for modern container layouts
sudo ./cgns-run -r 412 /usr/sbin/httpd
# Translates: /usr/sbin/httpd -> /usr/sbin/httpd (for containers)
#            /bin/bash -> /usr/bin/bash (for modern distros)
```

### Debugging Container Issues

Use the `-D/--debug` flag to troubleshoot namespace and cgroup operations:

```bash
sudo ./cgns-run -D 412 /bin/bash
```

Debug output shows:
- Cgroup joining attempts and results
- Namespace joining success/failure for each type
- Path translations when using `-r`
- Directory existence checks for cgroup operations

## Container Environment Support

This tool is specifically designed to work with modern container environments:

- **Podman/Docker containers**: Full namespace and cgroup support
- **Systemd services**: Handles systemd cgroup hierarchies
- **Modern distributions**: Automatic path translation for `/usr` merge layouts
- **User namespace handling**: Gracefully handles user namespace join failures

## Troubleshooting

### Common Issues

**User namespace join failures:**
```
Warning: Failed to join user namespace: Invalid argument
```
This is normal and expected for most processes. User namespaces are optional and many processes don't use them.

**Cgroup join failures:**
```
Failed to open cgroup file /sys/fs/cgroup/.../cgroup.procs: No such file or directory
```
Some cgroups may not exist or may not be accessible. This is handled gracefully and won't prevent execution.

**Command not found errors:**
```
Failed to exec httpd: No such file or directory
```
Try using the `-r` flag for automatic path translation, or use the full path to the executable within the container context.

### Verification Commands

```bash
# Verify process is running in correct context
cgns-run -d <new_process_pid> <target_pid>

# List all process namespaces for comparison
cgns-run -l <pid>

# Check if processes share the same container environment
ps aux | grep <command>
cgns-run -d <parent_pid> <child_pid>
```

## Security Considerations

- Requires appropriate privileges (typically root)
- Validates target PID exists before attempting operations
- Continues execution even if some cgroups cannot be joined (non-fatal)
- User namespace joins may fail safely (non-fatal warning)

## Cleaning up

```bash
make clean
```

## License

MIT License - see [LICENSE](LICENSE) file for details.