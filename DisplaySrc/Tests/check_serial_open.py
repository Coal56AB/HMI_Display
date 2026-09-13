"""Verify data links set modem lines before opening the OS port."""
import sys
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Simulator'))
from uart_protocol import open_port
from uart_server import Link
class Port:
    def __init__(self, port=None, **kwargs):
        assert port is None
        self.port = port
        self.dtr = self.rts = True
        self.opened = False
    def open(self):
        assert self.port == 'COM_TEST' and self.dtr is False and self.rts is False
        self.opened = True
    def reset_input_buffer(self):
        assert self.opened
    def close(self):
        self.opened = False
with patch('serial.Serial', Port):
    port = open_port('COM_TEST', .05)
    assert port.opened
    port.close()
    link = Link()
    link.connect('COM_TEST')
    assert link.serial.opened
    link.disconnect()
print('Serial PASS: terminal and simulator open with DTR/RTS disabled in advance')
