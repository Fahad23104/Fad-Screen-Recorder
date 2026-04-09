Fad Screen Recorder Pro 🎥
A high-performance, lightweight screen recording application built in modern C++.

Fad Screen Recorder Pro is engineered to capture high-fidelity video and audio without the heavy resource overhead of standard built-in recorders. Inspired by industry standards like Bandicam, it utilizes direct GPU-to-CPU texture mapping, native Windows audio sessions, and hardware-accelerated FFmpeg encoding to deliver flawlessly synced .mp4 recordings with zero dropped frames.

✨ Key Features
Ultra-Low Latency Capture: Utilizes DirectX 11 (DXGI Desktop Duplication) to pull frames directly from the GPU VRAM, ensuring minimal impact on system performance.

Advanced Audio Routing (WASAPI):

Record System Audio (Speakers/Loopback).

Record Microphone Audio (Direct Capture).

Dual-Mix Mode: Mathematically mixes floating-point speaker and microphone audio streams in real-time with anti-clipping safeguards.

Live Webcam Overlay (Picture-in-Picture): Uses Windows Media Foundation to natively hook your webcam. Features a custom CPU-side nearest-neighbor scaler and alpha-blending to draw circular or square camera masks seamlessly onto the video frame.

Dynamic Visual Effects:

Hardware cursor capture via GDI overlay.

Zero-latency visual mouse clicks (Yellow for Left-click, Cyan for Right-click).

Seamless Pause & Resume: Advanced chronological timestamp calculation allows you to pause and resume recordings without corrupting the FFmpeg pipeline or causing audio desync.

Professional Dashboard (ImGui): A sleek, dark-mode user interface featuring a persistent settings menu, an integrated video gallery, and a floating, borderless mini-widget for live recording status.

Global Hotkeys: Control the engine from anywhere without opening the UI:

F6 - Start / Resume Recording

F7 - Pause Recording

F8 - Stop & Save Recording

🛠️ Technology Stack
This project is built from the ground up using C++20 and relies on the following core libraries and APIs:

Language: C++20

Graphics & UI: DirectX 11, DXGI, Dear ImGui (Win32/DX11 backends)

Audio & Camera: Windows Audio Session API (WASAPI), Windows Media Foundation (MFPlat, MFReadWrite)

Encoding Engine: FFmpeg (libavcodec, libavformat, libavutil, libswresample, libswscale, libx264)

Configuration: nlohmann/json (for persistent config.json settings)

Package Management: vcpkg

🚀 Installation & Usage
Pre-Compiled Installer
For end-users, download the latest FadScreenRecorder_Setup.exe from the Releases tab. Install the application and launch it.

How to Record
Configure Settings: Open the app and navigate to the Settings tab. Adjust your Target FPS (dynamically capped to your monitor's refresh rate), Video Quality (CRF), Audio Routing, and Webcam preferences.

Set Output Folder: In the Capture Engine tab, select where you want your recordings saved (Defaults to Documents\Fad Screen Recording).

Record: Click Start Recording or press F6. The main window will hide, and a mini-widget will appear.

Review: Once stopped (via F8), navigate to the Video Gallery tab to double-click and play your new .mp4 file instantly.

🏗️ Building from Source
To compile Fad Screen Recorder Pro yourself, you will need Visual Studio 2022 with the "Desktop development with C++" workload installed.

Prerequisites
Install vcpkg and integrate it with Visual Studio.

Install the required dependencies via vcpkg:

Bash
vcpkg install ffmpeg[core,x264]:x64-windows
vcpkg install imgui[core,dx11,win32-binding]:x64-windows
vcpkg install nlohmann-json:x64-windows
Compilation Steps
Clone the repository and open the .sln file in Visual Studio 2022.

Ensure your build configuration is set to Release and x64.

Right-click the project -> Properties -> C/C++ -> Language, and ensure the C++ Standard is set to ISO C++20 Standard (/std:c++20).

To ensure the app builds as a silent GUI rather than a console app, navigate to Linker -> System and set SubSystem to Windows (/SUBSYSTEM:WINDOWS). Then navigate to Linker -> Advanced and set Entry Point to mainCRTStartup.

Click Build Solution.

Note: Ensure the FFmpeg .dll files (avcodec-61.dll, avformat-61.dll, etc.) are placed in the same directory as the compiled .exe before running.

👨‍💻 Author
Muhammad Fahad Irfan Computer Engineering, COMSATS

Developed with a focus on performance, mathematical precision, and clean C++ architectures.