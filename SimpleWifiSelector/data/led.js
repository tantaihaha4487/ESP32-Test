document.addEventListener('DOMContentLoaded', () => {
    const ledBtn = document.getElementById('ledBtn');
    const ledStatus = document.getElementById('ledStatus');

    function updateLedState() {
        fetch('/led')
            .then(res => res.json())
            .then(data => {
                const isOn = data.on;
                ledStatus.textContent = isOn ? 'LED is ON' : 'LED is OFF';
                ledBtn.textContent = isOn ? 'Turn OFF' : 'Turn ON';
                ledBtn.className = isOn ? 'btn on' : 'btn off';
            })
            .catch(err => console.error(err));
    }

    ledBtn.addEventListener('click', () => {
        const isCurrentlyOn = ledBtn.textContent === 'Turn OFF';
        const newState = isCurrentlyOn ? 'off' : 'on';
        fetch(`/led?state=${newState}`)
            .then(res => res.json())
            .then(data => {
                updateLedState();
            });
    });

    // Initial check
    updateLedState();
});
