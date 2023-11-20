# brctl
brctl over netlink protocol
#### Compile
```
gcc -o brctl brctl.c
```
#### Usage
```
./brctl [command] [args]
```
#### Commands
```
addbr [bridge_name]
delbr [bridge_name]
addif [bridge_name] [interface_name]
delif [bridge_name] [interface_name]
show
```
