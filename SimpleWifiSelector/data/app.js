document.addEventListener('DOMContentLoaded', () => {
    const statusDiv = document.getElementById('status');
    const networksDiv = document.getElementById('networks');
    const scanBtn = document.getElementById('scanBtn');
    const cfgForm = document.getElementById('cfg');
    const ssidInput = document.getElementById('ssid');
    const passInput = document.getElementById('pass');

    // Update status
    function updateStatus() {
        fetch('/status')
            .then(res => res.json())
            .then(data => {
                if (data.connected) {
                    statusDiv.textContent = `Connected to ${data.ssid} (${data.ip})`;
                    statusDiv.className = 'small success';
                } else {
                    statusDiv.textContent = 'Not connected';
                    statusDiv.className = 'small error';
                }
            })
            .catch(err => console.error(err));
    }

    updateStatus();
    // Poll status every 5 seconds
    setInterval(updateStatus, 5000);

    // Scan logic
    // Scan logic
    function startScan() {
        scanBtn.disabled = true;
        networksDiv.textContent = 'Scanning...';

        fetch('/scan_trigger', { method: 'POST' })
            .then(res => res.json())
            .then(data => {
                if (data.started) {
                    pollScanResults();
                } else {
                    networksDiv.textContent = 'Scan failed to start.';
                    scanBtn.disabled = false;
                }
            })
            .catch(err => {
                // If the AP restarts immediately, this fetch might fail.
                // We assume the scan started and try polling anyway.
                console.log('Scan trigger failed (AP restart?), polling anyway...');
                pollScanResults();
            });
    }

    scanBtn.addEventListener('click', startScan);

    function pollScanResults(retryCount = 0) {
        setTimeout(() => {
            fetch('/scan')
                .then(res => res.json())
                .then(data => {
                    if (data.length > 0 && data[0]._scanning) {
                        // Still scanning
                        pollScanResults(0);
                    } else {
                        renderNetworks(data);
                        scanBtn.disabled = false;
                    }
                })
                .catch(err => {
                    console.error('Poll error:', err);
                    if (retryCount < 20) {
                        // Retry for ~40 seconds (20 * 2000ms) to allow AP reconnection
                        const msg = retryCount > 0 ? ` (Waiting for connection... ${retryCount}/20)` : '';
                        networksDiv.textContent = 'Scanning...' + msg;
                        console.log(`Retrying poll (${retryCount + 1}/20)...`);
                        pollScanResults(retryCount + 1);
                    } else {
                        networksDiv.textContent = 'Error fetching results. Please reload.';
                        scanBtn.disabled = false;
                    }
                });
        }, 2000);
    }

    function renderNetworks(networks) {
        if (networks.length === 0) {
            networksDiv.textContent = 'No networks found.';
            return;
        }
        networksDiv.innerHTML = '';
        const list = document.createElement('ul');
        list.className = 'net-list';
        networks.forEach(net => {
            const li = document.createElement('li');
            li.textContent = `${net.ssid} (${net.rssi} dBm)${net.secure ? ' 🔒' : ''}`;
            li.addEventListener('click', () => {
                ssidInput.value = net.ssid;
                passInput.focus();
            });
            list.appendChild(li);
        });
        networksDiv.appendChild(list);
    }

    // Connect logic
    cfgForm.addEventListener('submit', (e) => {
        e.preventDefault();
        const ssid = ssidInput.value;
        const pass = passInput.value;

        if (!ssid) return;

        statusDiv.textContent = 'Connecting...';

        fetch('/connect', {
            method: 'POST',
            body: JSON.stringify({ ssid, pass })
        })
            .then(res => res.json())
            .then(data => {
                if (data.success) {
                    statusDiv.textContent = 'Credentials saved. Connecting...';
                    statusDiv.className = 'small success';

                    // Poll for connection status
                    let attempts = 0;
                    const checkInterval = setInterval(() => {
                        attempts++;
                        fetch('/status')
                            .then(res => res.json())
                            .then(status => {
                                if (status.connected && status.ip && status.ip !== '0.0.0.0') {
                                    clearInterval(checkInterval);

                                    // Clear previous content to show clean success message
                                    statusDiv.innerHTML = '';
                                    statusDiv.className = 'small success';

                                    const newUrl = `http://${status.ip}/`;

                                    const successMsg = document.createElement('div');
                                    successMsg.innerHTML = `<strong>Connection Successful!</strong><br>New IP: ${status.ip}<br><br>Please switch your device to the WiFi network <strong>${ssid}</strong>.<br>Then click the button below.`;
                                    statusDiv.appendChild(successMsg);

                                    const linkBtn = document.createElement('button');
                                    linkBtn.textContent = "Go to New IP";
                                    linkBtn.style.marginTop = "10px";
                                    linkBtn.style.padding = "10px 20px";
                                    linkBtn.style.backgroundColor = "#4CAF50";
                                    linkBtn.style.color = "white";
                                    linkBtn.style.border = "none";
                                    linkBtn.style.cursor = "pointer";
                                    linkBtn.onclick = () => {
                                        window.location.href = newUrl;
                                    };
                                    statusDiv.appendChild(linkBtn);
                                } else if (attempts > 20) { // Timeout after ~40 seconds
                                    clearInterval(checkInterval);
                                    statusDiv.textContent = 'Connection timed out. Please check credentials and try again.';
                                    statusDiv.className = 'small error';
                                }
                            })
                            .catch(e => console.log('Waiting for connection...'));
                    }, 2000);

                } else {
                    statusDiv.textContent = 'Error: ' + (data.message || 'Unknown');
                    statusDiv.className = 'small error';
                }
            })
            .catch(err => {
                statusDiv.textContent = 'Request failed.';
            });
    });

    // Auto start scan on load
    startScan();
});
