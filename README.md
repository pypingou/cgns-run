# cgns-run - Namespace and Cgroup Entry Tool

A C program that executes a command in the same namespaces and cgroups as an existing process.

## Features

- **List mode**: Display namespace and cgroup information for any process
- **Execute mode**: Attempts to join all Linux namespaces (mount, uts, ipc, pid, net, user, cgroup)
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
- `-c, --conmon`: Fork and exit parent (conmon-like behavior for container integration)
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

# Run httpd with conmon-like behavior (no host parent process)
sudo ./cgns-run -c -r 412 /usr/sbin/httpd -DFOREGROUND
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
- **Parent process**: Transitions to container namespaces during execution
- **Child processes**: Inherit the container namespaces from the parent
- **All processes**: Share identical namespaces with the target container process

### Namespace Inheritance

All processes started by `cgns-run` successfully join the target container's namespaces:

```bash
$ cgns-run -d <httpd_process> 412  # Any httpd process vs container process
NAMESPACES:
  mnt: SAME
  uts: SAME
  ipc: SAME
  pid: SAME                   # All in container PID namespace
  net: SAME
  user: SAME
  cgroup: SAME

SUMMARY:
  Namespaces: ALL SAME
  Cgroups: ALL SAME
```

**What this means:**
- The `cgns-run` process successfully joins all container namespaces
- When `execv()` is called, the new process inherits these namespaces
- All child processes spawned remain in the container namespaces
- Namespace joining works correctly, but container visibility is limited (see "Container Runtime Limitations")

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
- **Conmon-like behavior**: Optional fork-and-exit mode for cleaner process integration

## Container Runtime Limitations

**Important**: While `cgns-run` successfully joins all Linux namespaces and cgroups, processes started via `cgns-run` **will not be visible inside container tools** (like `podman exec`, `docker exec`) even though they share identical namespaces.

### Why This Happens

Modern container runtimes implement additional process isolation beyond Linux namespaces:

- **Runtime filtering**: Container tools filter process visibility based on process ancestry
- **Security policies**: Additional isolation prevents external processes from appearing in containers
- **Cgroup-based visibility**: Process visibility may be tied to specific cgroup membership patterns

### What This Means

```bash
# This works - cgns-run joins all namespaces successfully
sudo ./cgns-run 412 /usr/sbin/httpd -DFOREGROUND

# Verification shows identical namespaces
cgns-run -d <httpd_worker_pid> 412
# Output: "Namespaces: ALL SAME, Cgroups: ALL SAME"

# But container tools won't see the httpd processes
podman exec container_name ps aux  # httpd processes NOT visible
```

This is **expected behavior** - it's a security feature of container runtimes, not a limitation of `cgns-run`.

## Conmon Mode

The `-c/--conmon` flag provides an alternative execution mode inspired by container runtime monitors:

### How Conmon Mode Works

```bash
sudo ./cgns-run -c 412 /usr/sbin/httpd -DFOREGROUND
```

1. **Join all namespaces** (same as normal mode)
2. **Fork a child process** in the container context
3. **Parent exits immediately** (no lingering host process)
4. **Child becomes session leader** (`setsid()`) and executes the command

### Benefits of Conmon Mode

- ✅ **No host parent process** - Parent exits cleanly after fork
- ✅ **Session isolation** - Child process becomes independent session leader
- ✅ **Process detachment** - Command runs detached from controlling terminal
- ✅ **Clean process tree** - No cgns-run parent visible in process lists

### Conmon Mode vs Normal Mode

| Aspect | Normal Mode | Conmon Mode |
|--------|-------------|-------------|
| Host parent visible | Yes (`cgns-run` process remains) | No (parent exits immediately) |
| TTY attachment | Attached to terminal | Detached (`?` TTY) |
| Process status | `S+` (foreground) | `Ss` (session leader) |
| Container visibility | Not visible in container tools | Still not visible (runtime limitation) |

**Note**: Even with conmon mode, processes remain invisible to container tools due to container runtime security policies, but the approach provides cleaner process lifecycle management.

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

**Container process visibility:**
```
podman exec container_name ps aux  # Started processes not visible
```
This is expected behavior. Processes started via `cgns-run` share all namespaces with the container but remain external to the container runtime's process management. See "Container Runtime Limitations" section.

### Verification Commands

```bash
# Verify process is running in correct context (should show "ALL SAME")
cgns-run -d <new_process_pid> <target_pid>

# List all process namespaces for comparison
cgns-run -l <pid>

# Check if processes share the same container environment
ps aux | grep <command>
cgns-run -d <process_pid> <container_pid>

# Note: Container tools won't show cgns-run processes
podman exec container_name ps aux  # Will NOT show cgns-run processes
docker exec container_name ps aux   # Will NOT show cgns-run processes
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