const API_URL = 'http://127.0.0.1:8080/stats';

let cpuData = [];
let memoryData = [];
let labels = [];

let cpuChart = null;
let memChart = null;

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
                ticks: { color: '#e0e0e0', maxTicksLimit: 5 }
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
        document.getElementById('last-updated').textContent = now.toLocaleTimeString();

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
        data.processes.forEach((proc, index) => {
            const row = document.createElement('div');
            row.className = 'stat-row';
            
            let name = proc.name;
            if (name.length > 20) name = name.substring(0, 17) + '...';
            
            row.textContent = `| ${index + 1}. ${name} (${proc.pid}) - `;
            
            const span = document.createElement('span');
            span.textContent = `${proc.cpu.toFixed(1)}%`;
            if (proc.cpu > 85) {
                span.className = 'warning';
            }
            
            row.appendChild(span);
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

    if (labels.length > 20) {
        labels.shift();
        cpuData.shift();
        memoryData.shift();
    }

    if (cpuChart && memChart) {
        cpuChart.update();
        memChart.update();
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
