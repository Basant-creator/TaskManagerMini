const API_URL = 'http://127.0.0.1:8080/stats';

let cpuData = [];
let memoryData = [];
let netData = [];
let labels = [];

let cpuChart = null;
let memChart = null;
let netChart = null;

function initCharts() {
    const commonOptions = {
        responsive: true,
        maintainAspectRatio: false,
        animation: {
            duration: 400
        },
        scales: {
            x: {
                grid: { color: '#333' },
                ticks: { display: false }
            },
            y: {
                min: 0,
                max: 100,
                grid: { color: '#333' },
                ticks: { color: '#e0e0e0' }
            }
        },
        plugins: {
            legend: { display: false }
        },
        elements: {
            line: { tension: 0.4, borderWidth: 2 },
            point: { radius: 0 }
        }
    };

    const cpuCtx = document.getElementById('cpu-chart').getContext('2d');
    cpuChart = new Chart(cpuCtx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [{
                data: cpuData,
                borderColor: '#55ff55',
                backgroundColor: 'rgba(85, 255, 85, 0.1)',
                fill: true
            }]
        },
        options: commonOptions
    });

    const memCtx = document.getElementById('mem-chart').getContext('2d');
    memChart = new Chart(memCtx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [{
                data: memoryData,
                borderColor: '#55ff55',
                backgroundColor: 'rgba(85, 255, 85, 0.1)',
                fill: true
            }]
        },
        options: commonOptions
    });

    const netOptions = JSON.parse(JSON.stringify(commonOptions));
    delete netOptions.scales.y.max;
    
    const netCtx = document.getElementById('net-chart').getContext('2d');
    netChart = new Chart(netCtx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [{
                data: netData,
                borderColor: '#55ff55',
                backgroundColor: 'rgba(85, 255, 85, 0.1)',
                fill: true
            }]
        },
        options: netOptions
    });
}

async function fetchStats() {
    try {
        const response = await fetch(API_URL);
        if (!response.ok) throw new Error('Network response was not ok');
        
        const data = await response.json();
        
        const statusEl = document.getElementById('conn-status');
        if (statusEl.textContent !== 'Connected') {
            statusEl.textContent = 'Connected';
            statusEl.className = 'connected';
            logAlert('> Connection restored.');
        }
        
        const now = new Date();
        document.getElementById('last-updated').textContent = now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

        updateUI(data);
        updateCharts(data);
        updateAlerts(data);
    } catch (error) {
        const statusEl = document.getElementById('conn-status');
        if (statusEl.textContent !== 'Disconnected') {
            statusEl.textContent = 'Disconnected';
            statusEl.className = 'disconnected';
            logAlert('> Connection lost. Waiting for server...');
        }
        
        document.getElementById('cpu-usage').textContent = '--%';
        document.getElementById('mem-usage').textContent = '--%';
        document.getElementById('disk-usage').textContent = '--%';
        document.getElementById('net-sent').textContent = '-- Mbps';
        document.getElementById('net-recv').textContent = '-- Mbps';
        document.getElementById('cpu-usage').className = '';
        document.getElementById('mem-usage').className = '';
        document.getElementById('disk-usage').className = '';
        document.getElementById('process-list').innerHTML = '<div class="stat-row">| Waiting for connection...</div>';
    }
}

function updateUI(data) {
    const cpuEl = document.getElementById('cpu-usage');
    const memEl = document.getElementById('mem-usage');
    const diskEl = document.getElementById('disk-usage');

    cpuEl.textContent = data.cpu.toFixed(1) + '%';
    memEl.textContent = data.memory.toFixed(1) + '%';
    diskEl.textContent = data.disk.toFixed(1) + '%';

    cpuEl.className = data.cpu > 85 ? 'warning' : '';
    memEl.className = data.memory > 90 ? 'warning' : '';
    diskEl.className = data.disk > 90 ? 'warning' : '';
    
    document.getElementById('net-sent').textContent = data.network.sent_mbps.toFixed(2) + ' Mbps';
    document.getElementById('net-recv').textContent = data.network.received_mbps.toFixed(2) + ' Mbps';
    
    const processList = document.getElementById('process-list');
    processList.innerHTML = '';
    
    if (data.processes && data.processes.length > 0) {
        data.processes.forEach((proc) => {
            const row = document.createElement('div');
            row.className = 'process-row';
            
            const nameSpan = document.createElement('span');
            nameSpan.className = 'col-name';
            nameSpan.textContent = proc.name;
            
            const pidSpan = document.createElement('span');
            pidSpan.className = 'col-pid';
            pidSpan.textContent = proc.pid;
            
            const cpuSpan = document.createElement('span');
            cpuSpan.className = 'col-cpu';
            cpuSpan.textContent = proc.cpu.toFixed(1) + '%';
            if (proc.cpu > 85) cpuSpan.classList.add('warning');
            
            const memSpan = document.createElement('span');
            memSpan.className = 'col-mem';
            memSpan.textContent = proc.memory_mb ? proc.memory_mb.toFixed(1) + ' MB' : '0.0 MB';
            
            const actSpan = document.createElement('span');
            actSpan.className = 'col-act';
            const killBtn = document.createElement('button');
            killBtn.className = 'kill-btn';
            killBtn.textContent = '[X]';
            killBtn.onclick = () => killProcess(proc.pid, proc.name);
            actSpan.appendChild(killBtn);
            
            row.appendChild(nameSpan);
            row.appendChild(pidSpan);
            row.appendChild(cpuSpan);
            row.appendChild(memSpan);
            row.appendChild(actSpan);
            
            processList.appendChild(row);
        });
    } else {
        const row = document.createElement('div');
        row.className = 'stat-row';
        row.textContent = '| No processes found.';
        processList.appendChild(row);
    }
}

function updateCharts(data) {
    const now = new Date();
    const timeLabel = now.toLocaleTimeString();

    labels.push(timeLabel);
    cpuData.push(data.cpu);
    memoryData.push(data.memory);
    netData.push(data.network.sent_mbps + data.network.received_mbps);

    if (labels.length > 20) {
        labels.shift();
        cpuData.shift();
        memoryData.shift();
        netData.shift();
    }

    if (cpuChart && memChart && netChart) {
        cpuChart.update();
        memChart.update();
        netChart.update();
    }
}

function updateAlerts(data) {
    if (data.cpu > 85) {
        logAlert(`[WARNING] High CPU usage: ${data.cpu.toFixed(1)}%`);
    }
    if (data.memory > 90) {
        logAlert(`[WARNING] High Memory usage: ${data.memory.toFixed(1)}%`);
    }
    if (data.disk > 90) {
        logAlert(`[WARNING] High Disk usage: ${data.disk.toFixed(1)}%`);
    }
}

async function killProcess(pid, name) {
    if (!confirm(`Are you sure you want to terminate ${name} (PID: ${pid})?`)) return;
    
    try {
        const response = await fetch(`http://127.0.0.1:8080/kill?pid=${pid}`);
        const result = await response.json();
        
        if (result.success) {
            logAlert(`> Successfully terminated ${name} (PID: ${pid})`);
        } else {
            logAlert(`> Failed to terminate ${name} (PID: ${pid}). It might require admin privileges.`);
        }
    } catch (error) {
        logAlert(`> Error attempting to terminate process ${pid}`);
    }
}

function logAlert(message) {
    const alertsContainer = document.getElementById('alerts-container');
    const logEntry = document.createElement('div');
    logEntry.className = 'log-entry';
    
    const now = new Date();
    logEntry.textContent = `| [${now.toLocaleTimeString()}] ${message}`;
    
    alertsContainer.appendChild(logEntry);
    
    while (alertsContainer.children.length > 5) {
        alertsContainer.removeChild(alertsContainer.firstChild);
    }
}

setInterval(fetchStats, 1000);
initCharts();
fetchStats();
