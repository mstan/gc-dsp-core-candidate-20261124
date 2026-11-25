# Native/HLE qualification

The paired gc-wii-core qualification attachment builds this extracted DSP service against caller-supplied pinned BlueWake DSP targets. The mail fixture uses synthetic zero-filled image buffers: HLE provides the ROM mailbox service, so it requires no dumped firmware. It verifies lifecycle, high/low publication, mailbox echo, snapshot restore/rejection, terminal observation failure and zero interpreted retirement. It does not establish real-game audio quality or missing Wii memory bindings.
