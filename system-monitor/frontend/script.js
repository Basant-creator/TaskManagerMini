const API_URL = 'http://127.0.0.1:8080/stats';

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

        updateDashboard(data);
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

function updateDashboard(data) {
    const cpuEl = document.getElementById('cpu-usage');
    const memEl = document.getElementById('mem-usage');
    const diskEl = document.getElementById('disk-usage');

    cpuEl.textContent = data.cpu.toFixed(1) + '%';
    memEl.textContent = data.memory.toFixed(1) + '%';
    diskEl.textContent = data.disk.toFixed(1) + '%';

    cpuEl.className = data.cpu > 85 ? 'warning' : '';
    memEl.className = data.memory > 85 ? 'warning' : '';
    diskEl.className = data.disk > 85 ? 'warning' : '';
    
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
fetchStats();
