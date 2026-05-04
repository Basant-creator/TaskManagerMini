const API_URL = 'http://127.0.0.1:8080/stats';

async function fetchStats() {
    try {
        const response = await fetch(API_URL);
        if (!response.ok) throw new Error('Network response was not ok');
        
        const data = await response.json();
        updateDashboard(data);
    } catch (error) {
        logAlert('> Error fetching data: ' + error.message);
    }
}

function updateDashboard(data) {
    // Update core stats
    document.getElementById('cpu-usage').textContent = data.cpu.toFixed(1) + '%';
    document.getElementById('mem-usage').textContent = data.memory.toFixed(1) + '%';
    document.getElementById('disk-usage').textContent = data.disk.toFixed(1) + '%';
    
    // Update network stats
    document.getElementById('net-sent').textContent = data.network.sent_mbps.toFixed(2) + ' Mbps';
    document.getElementById('net-recv').textContent = data.network.received_mbps.toFixed(2) + ' Mbps';
    
    // Update processes
    const processList = document.getElementById('process-list');
    processList.innerHTML = '';
    
    if (data.processes && data.processes.length > 0) {
        data.processes.forEach((proc, index) => {
            const row = document.createElement('div');
            row.className = 'stat-row';
            
            let name = proc.name;
            if (name.length > 20) name = name.substring(0, 17) + '...';
            
            row.textContent = `| ${index + 1}. ${name} (${proc.pid}) - ${proc.cpu.toFixed(1)}%`;
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
    logEntry.textContent = '| ' + message;
    
    alertsContainer.appendChild(logEntry);
    
    while (alertsContainer.children.length > 5) {
        alertsContainer.removeChild(alertsContainer.firstChild);
    }
}

// Fetch stats every second
setInterval(fetchStats, 1000);
fetchStats();
