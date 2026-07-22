# Installation

## Supported toolchains

RBF-Safe requires CMake 3.23 or newer and a C++20 compiler. CI covers GCC and
Clang on Ubuntu 22.04/24.04 and MSVC on Windows Server 2022. Python wheels are
built for CPython 3.10–3.12 on Linux and Windows.

The core C++ library has no public third-party runtime dependency. pybind11 and
scikit-build-core are build dependencies only for the Python extension.

## Build from source

Single-config generators such as Ninja or Unix Makefiles:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DRBFSAFE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Visual Studio and other multi-config generators:

```powershell
cmake -S . -B build -A x64 -DRBFSAFE_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Available CMake options:

| Option | Default | Purpose |
|---|---:|---|
| `RBFSAFE_BUILD_TESTS` | `ON` | Build CTest targets |
| `RBFSAFE_BUILD_EXAMPLES` | `ON` | Build the C++ quickstart |
| `RBFSAFE_BUILD_TOOLS` | `ON` | Build `rbfsafe-inspect` |
| `RBFSAFE_BUILD_PYTHON` | `OFF` | Build the pybind11 extension |
| `RBFSAFE_BUILD_OMPL` | `OFF` | Build the optional C++ OMPL adapter |
| `RBFSAFE_WARNINGS_AS_ERRORS` | `OFF` | Promote project warnings to errors |

## Install and consume with CMake

```bash
cmake --install build --config Release --prefix "$PWD/install"
```

Downstream `CMakeLists.txt`:

```cmake
find_package(RBFSafe CONFIG REQUIRED)
add_executable(my_consumer main.cpp)
target_link_libraries(my_consumer PRIVATE RBFSafe::rbfsafe)
```

Configure the consumer with `-DCMAKE_PREFIX_PATH=/path/to/install`. Individual
targets `RBFSafe::geometry`, `RBFSafe::lect`, `RBFSafe::atlas`,
`RBFSafe::update`, `RBFSafe::corridor`, `RBFSafe::ik`, and
`RBFSafe::regions` are available when the aggregate target
is unnecessary.

`RBFSafe::corridor` is part of the core installation and introduces no third-
party dependency. It provides OBB, Portal, and HiPaC APIs. The aggregate
`RBFSafe::rbfsafe` target now includes it.

`RBFSafe::regions` provides the unified region/certificate database, OBB Atlas
builder, arbitrary AABB/OBB Portal discovery, and higher-order region APIs.
It has no third-party dependency.

`RBFSafe::update` provides dynamic scene differences, local Atlas repair, and
version-store APIs without adding third-party dependencies.

To build the optional adapter, install OMPL and configure with
`-DRBFSAFE_BUILD_OMPL=ON`. Installed consumers request the component explicitly:

```cmake
find_package(RBFSafe CONFIG REQUIRED COMPONENTS ompl)
target_link_libraries(my_planner PRIVATE RBFSafe::ompl)
```

The package accepts OMPL configurations that provide the current `ompl::ompl`
target or the legacy `OMPL_INCLUDE_DIRS` and `OMPL_LIBRARIES` variables. The
base RBF-Safe package and Python wheels do not load OMPL.

## Build the optional MoveIt 2 package

Install RBF-Safe to a prefix first. On Ubuntu 24.04 with ROS 2 Jazzy and
MoveIt 2 sourced:

```bash
source /opt/ros/jazzy/setup.bash
colcon build --base-paths plugins/moveit2 \
  --packages-select rbfsafe_moveit \
  --cmake-args -DCMAKE_PREFIX_PATH=/path/to/rbfsafe/install
colcon test --packages-select rbfsafe_moveit
```

The ament package is deliberately outside the core CMake graph. See the
[MoveIt 2 guide](moveit2.md) for its fail-closed behavior and parameters.

## Build and install the Python wheel

Until wheels are attached to a release, build from a checkout:

```bash
python -m venv .venv
# Linux/macOS: source .venv/bin/activate
# Windows PowerShell: .venv\Scripts\Activate.ps1
python -m pip install --upgrade pip build
python -m build --wheel
python -m pip install dist/rbfsafe-*.whl
python -c "import rbfsafe; print(rbfsafe.__version__)"
```

Install the optional plotting dependency with
`python -m pip install "matplotlib>=3.7"` after installing a wheel.

## Verify an installation

For C++, build `tests/consumer` against the installed prefix. For Python, run:

```bash
pytest tests/test_python.py
rbfsafe-inspect --help
```

Build directories, wheels, virtual environments, and generated Atlas
directories are intentionally ignored by Git.
