# System Monitor Dashboard

A lightweight, real-time Windows system monitoring tool featuring a native C HTTP backend server and a sleek, terminal-styled HTML/CSS/JS frontend dashboard.

## Features

- **Real-Time Graphs**: Live plotting of CPU, Memory, and Network usage using Chart.js.
- **Top Processes Tracking**: Displays the top 10 most memory-intensive processes currently running.
- **Task Management**: Built-in functionality to forcefully terminate (kill) unwanted processes directly from the dashboard.
- **System Specifications**: Automatically queries the Windows Registry to display exact hardware specs (CPU Model, Total RAM, Total Disk Space).
- **Responsive Terminal Aesthetics**: A visually stunning, hacker-themed dark mode UI that adapts to wide screens and feels lightning fast.
- **Alerts System**: Triggers and logs warnings if CPU, Memory, or Disk usage exceeds predefined safety thresholds (>85% and >90%).

## Architecture

1. **Backend (`server.c`)**: A standalone C program that uses native Windows APIs (Winsock2, PSAPI, Iphlpapi) to query system telemetry and serves it over a simple HTTP GET endpoint (`http://127.0.0.1:8080/stats`). It also handles `GET /kill?pid=...` to terminate processes.
2. **Frontend (`frontend/`)**: A pure vanilla JavaScript, HTML, and CSS application. It dynamically fetches telemetry from the C backend every 1000ms and updates the DOM and canvas charts.

## Requirements

- Windows OS
- MinGW / GCC Compiler

## How to Build

Navigate to the project root directory and compile the C backend using `gcc`. You must include the required Windows library flags:

```bash
gcc server.c -o server.exe -lws2_32 -lpsapi -liphlpapi -ladvapi32
```

## How to Run

1. **Start the Backend:**
   Run the newly compiled executable. This will start the background HTTP server on port 8080.
   ```bash
   ./server.exe
   ```

2. **Open the Dashboard:**
   Simply double-click the `frontend/index.html` file to open it in your web browser of choice. The dashboard will automatically connect to the backend and begin streaming live metrics.

## Thresholds & Alerts

The dashboard actively monitors for system strain and will flag metrics in red and output a warning log if they exceed:
- CPU Usage > 85%
- Memory Usage > 90%
- Disk Usage > 90%
