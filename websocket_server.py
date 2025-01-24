import asyncio
import websockets
import json
import base64

async def handle_message(message):
    try:
        data = json.loads(message)
        print("Received message:", data)
        if "audio" in data:
            audio_base64 = data["audio"]
            audio_data = base64.b64decode(audio_base64)
            with open("audio_output.pcm", "ab") as audio_file:
                audio_file.write(audio_data)
        response = json.dumps({"status": "ok"})
    except Exception as e:
        response = json.dumps({"status": "error", "message": str(e)})
    return response

async def websocket_handler(websocket, path=None):
    print(f"New connection from {websocket.remote_address} on path {path}")
    async for message in websocket:
        response = await handle_message(message)
        await websocket.send(response)

async def main():
    print("Starting WebSocket server on ws://0.0.0.0:8765")
    async with websockets.serve(websocket_handler, "0.0.0.0", 8765):
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    asyncio.run(main())