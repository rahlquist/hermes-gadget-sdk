import asyncio

from hermes_gadget_plugin.hub import AudioOut, PLAYBACK_LEAD_S


class _Sink:
    speaker_rate = 16000

    def __init__(self):
        self.sent_at = []

    async def send_json(self, _message):
        return None

    async def send_binary(self, _data):
        self.sent_at.append(asyncio.get_running_loop().time())


def test_audio_out_paces_burst_after_provider_stall():
    async def exercise():
        sink = _Sink()
        out = AudioOut(sink, stream=1, src_rate=16000)
        await out.start()
        await asyncio.sleep(0.75)  # Provider stall before its next burst
        out.write(bytes(16000 * 2 * 2))  # Two seconds of mono PCM16
        out.finish()
        await asyncio.wait_for(out.done.wait(), timeout=5)
        return sink.sent_at

    sent_at = asyncio.run(exercise())
    assert len(sent_at) == 50  # 40 ms frames over two seconds
    assert sent_at[-1] - sent_at[0] >= 2.0 - PLAYBACK_LEAD_S - 0.12
