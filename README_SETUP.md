## Cosmic Engine - Quick Start Guide

## Windows:

### Prerequisites & Requirements

- **Windows:** Any modern version of **Microsoft Visual Studio (2019, 2022, or 2026)** with the "Desktop development with C++" workload installed **OR** a standalone C++ compiler (like MinGW-w64).

---

## 🛠️ Step 1: The First-Time Setup (Once Only)

### **Windows**

1. Double-click **`setup.bat`** (in the engine root).
   :

---

## Step 2: Building and Running Your Game

### Scenario A: Your project lives INSIDE the engine repo (e.g., Projects/)

#### **Windows**

1. Double-click **`build_all.bat`** (in the engine root).
2. Go to `build/Runtime/Debug/` and double-click **`CosmicApp.exe`**.
   - (Recommended) Or you can open the root Cosmic Folder in Microsoft Visual Studio and then click on the f5 dropdown and select CosmicApp.exe

---

### Scenario B: Your project lives OUTSIDE the repository (like on your Desktop)

#### **Windows**

1. Double-click **`build_engine.bat`** (in the engine root).
2. Go to your standalone project folder and double-click its local **`build_project.bat`**.
3. Go back to your engine root, navigate to `build/Runtime/Debug/`, and double-click **`CosmicApp.exe`**.
   - (Recommended) Or you can open the root Cosmic Folder in Microsoft Visual Studio and then click on the f5 dropdown and select CosmicApp.exe

---

## Mac - Engine Doesn't Natively Support this right now (a lot of glfw and windows specific stuff)
### But you can build it in theory... maybe kinda sorta

### Prerequisites & Requirements

- **Mac / Linux:** A working C++17 compiler (GCC or Clang) and CMake (3.21+) installed on your system path.

---

## 🛠️ Step 1: The First-Time Setup (Once Only)

### **Mac / Linux**

1. Open a terminal in the engine root folder.
2. Run this command to set up your pathing variable:

```bash
export COSMIC_SDK=$(pwd)

```

_(To make this permanent, add that line to your `~/.bashrc` or `~/.zshrc` file)._

---

## Step 2: Building and Running Your Game

### Scenario A: Your project lives INSIDE the engine repo (e.g., Projects/)

#### **Mac / Linux**

1. Open a terminal in the engine root and run:

```bash
cmake -B build -DCOSMIC_BUILD_ENGINE_ONLY=OFF
cmake --build build --config Debug --parallel

```

2. Run the executable found in `build/Runtime/Debug/CosmicApp`.

---

### Scenario B: Your project lives OUTSIDE the repository (like on your Desktop)

#### **Mac / Linux**

1. Open a terminal in the engine root and build the core:

```bash
   cmake -B build -DCOSMIC_BUILD_ENGINE_ONLY=ON
   cmake --build build --config Debug --parallel

```

2. Open a terminal in your standalone project folder and build it:

```bash
   cmake -B build -DCOSMIC_SDK_DIR=$COSMIC_SDK
   cmake --build build --config Debug --parallel

```

3. Go back to your engine root and run `build/Runtime/Debug/CosmicApp`.

---
