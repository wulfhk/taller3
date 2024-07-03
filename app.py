import cv2
import numpy as np
from flask import Flask, Response, render_template, request, jsonify
import threading
from ultralytics import YOLO
from collections import deque
import time

app = Flask(__name__)

frame_buffer = deque(maxlen=5)
model_semaphore = threading.Semaphore(1)
model = YOLO('xddd.pt')

aggression_detected = False

def process_frame(frame_data):
    global aggression_detected
    nparr = np.frombuffer(frame_data, np.uint8)
    frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
    if frame is not None:
        with model_semaphore:
            results = model(frame, conf=0.7)  # Set confidence threshold to 0.7
        
        # Filter results for class 1 (violence) only
        violence_detections = [det for det in results[0].boxes.data if int(det[5]) == 1]
        
        aggression_detected = len(violence_detections) > 0
        
        annotated_frame = frame.copy()
        for det in violence_detections:
            bbox = det[:4].int().cpu().numpy()
            cv2.rectangle(annotated_frame, (bbox[0], bbox[1]), (bbox[2], bbox[3]), (0, 0, 255), 2)
        
        if aggression_detected:
            cv2.putText(annotated_frame, "AGRESION", (10, 50), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255), 2)
        
        frame_buffer.append(annotated_frame)

@app.route('/video_feed', methods=['POST'])
def video_feed():
    if request.content_type.startswith('image/jpeg'):
        threading.Thread(target=process_frame, args=(request.data,)).start()
        return 'OK', 200
    return 'Unsupported Media Type', 415

@app.route('/check_aggression', methods=['GET'])
def check_aggression():
    response = {"aggression": "true" if aggression_detected else "false"}
    print(f"Aggression check: {response}")  # Add this line
    return jsonify(response)

def generate_frames():
    while True:
        if frame_buffer:
            frame = frame_buffer[-1].copy()
            encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), 85]
            ret, buffer = cv2.imencode('.jpg', frame, encode_param)
            frame = buffer.tobytes()
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')
        else:
            time.sleep(0.01)

@app.route('/stream')
def stream():
    return Response(generate_frames(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/')
def index():
    return render_template('index.html')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080, threaded=True)