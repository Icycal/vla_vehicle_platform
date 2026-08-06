# ROS Workspace Environments

## Why a clean shell is required

The vehicle has several ROS 2 workspaces. Sourcing all of their `setup.bash`
files in one terminal mixes package indexes, shared libraries, Python modules,
and plugin descriptions. Source order then determines which implementation is
used.

The older workspace `setup.bash` files also remember the underlays that were
present when they were built. For example, sourcing the Wheeltec
`install/setup.bash` can pull Autoware and RHZD back into an otherwise clean
shell. The environment tool therefore:

1. starts from `env -i` instead of inheriting the current ROS environment;
2. sources `/opt/ros/humble/setup.bash`;
3. loads only the selected workspaces with `local_setup.bash`;
4. sets the profile's ROS Domain and Cyclone DDS;
5. either opens an interactive shell or runs one command.

## Profiles

| Profile | Loaded workspaces | Default domain |
| --- | --- | ---: |
| `base` | ROS 2 Humble | 0 |
| `wheeltec` | ROS 2 Humble + Wheeltec | 0 |
| `autoware` | ROS 2 Humble + Wheeltec + Autoware | 0 |
| `rhzd` | ROS 2 Humble + Wheeltec + Autoware + RHZD | 0 |
| `vla` | ROS 2 Humble + VLA vehicle platform | 43 |

The `vla` profile intentionally does not load the legacy Wheeltec, Autoware, or
RHZD installs. It uses the fixed source snapshots built inside this repository.

## Interactive use

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/ros_env.sh --list
./scripts/ros_env.sh vla
```

The prompt identifies the active profile and domain:

```text
[ros:vla|domain:43] wheeltec@wheeltec:~$
```

Run `exit` to return to the original terminal. Use separate clean shells for
legacy work:

```bash
./scripts/ros_env.sh wheeltec
./scripts/ros_env.sh autoware
./scripts/ros_env.sh rhzd
```

## One-command use

```bash
./scripts/ros_env.sh vla -- ros2 pkg prefix usb_cam
./scripts/ros_env.sh vla --domain 42 -- ros2 node list
./scripts/ros_env.sh rhzd -- ros2 pkg prefix rhzd_assist
```

Project build, launch, check, and episode scripts call
`ensure_vla_environment.sh`. They automatically restart themselves inside the
clean `vla` profile with the required ROS Domain, even when invoked from a
terminal whose `.bashrc` already sourced legacy workspaces.

## Bash configuration

The environment tool is safe to use before cleaning `.bashrc`, because it does
not inherit ROS paths. Long term, keep only the base ROS setup in `.bashrc` and
remove automatic sourcing of individual workspaces. Optional aliases may call
the tool without sourcing a workspace directly:

```bash
alias ros-vla='/home/wheeltec/vla_vehicle_platform/scripts/ros_env.sh vla'
alias ros-wheeltec='/home/wheeltec/vla_vehicle_platform/scripts/ros_env.sh wheeltec'
alias ros-autoware='/home/wheeltec/vla_vehicle_platform/scripts/ros_env.sh autoware'
alias ros-rhzd='/home/wheeltec/vla_vehicle_platform/scripts/ros_env.sh rhzd'
```
