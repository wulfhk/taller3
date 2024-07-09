import cv2
import numpy as np
from flask import Flask, Response, render_template, request, jsonify, url_for
import threading
from ultralytics import YOLO
from collections import deque
import time
import psycopg2
from psycopg2 import Binary
import os
from datetime import datetime
import logging
logging.basicConfig(level=logging.DEBUG)

app = Flask(__name__)

# Configuración de la base de datos
DB_CONFIG = {
    'dbname': 'detecciones',
    'user': 'gean',
    'password': '123',
    'host': '34.29.7.1',
    'port': '5432'
}

frame_buffer = deque(maxlen=5)
model_semaphore = threading.Semaphore(1)
model = YOLO('xddd.pt')
aggression_detected = False

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

def save_detection(image):
    conn = get_db_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("INSERT INTO detecciones (imagen) VALUES (%s)", (Binary(image),))
        conn.commit()
    except Exception as e:
        print(f"Error al guardar la detección: {e}")
    finally:
        conn.close()

def process_frame(frame_data):
    global aggression_detected
    nparr = np.frombuffer(frame_data, np.uint8)
    frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
    if frame is not None:
        with model_semaphore:
            results = model(frame, conf=0.7)
        
        violence_detections = [det for det in results[0].boxes.data if int(det[5]) == 1]
        
        aggression_detected = len(violence_detections) > 0
        
        annotated_frame = frame.copy()
        for det in violence_detections:
            bbox = det[:4].int().cpu().numpy()
            cv2.rectangle(annotated_frame, (bbox[0], bbox[1]), (bbox[2], bbox[3]), (0, 0, 255), 2)
        
        if aggression_detected:
            cv2.putText(annotated_frame, "AGRESION", (10, 50), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255), 2)
            
            # Guardar la imagen en la base de datos
            _, buffer = cv2.imencode('.jpg', annotated_frame)
            save_detection(buffer.tobytes())
        
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
    print(f"Aggression check: {response}")
    return jsonify(response)

def generate_frames():
    while True:
        if frame_buffer:
            frame = frame_buffer.popleft()
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

@app.route('/detecciones')
def detecciones():
    conn = get_db_connection()
    detecciones = []
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT id, fecha_hora FROM detecciones ORDER BY fecha_hora DESC")
            detecciones = cur.fetchall()
        print(f"Número de detecciones recuperadas: {len(detecciones)}")  # Añade este print para debugging
    except Exception as e:
        print(f"Error al obtener las detecciones: {e}")
    finally:
        conn.close()
    return render_template('detecciones.html', detecciones=detecciones)

@app.route('/imagen/<int:id>')
def imagen(id):
    conn = get_db_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT imagen FROM detecciones WHERE id = %s", (id,))
            resultado = cur.fetchone()
            if resultado:
                imagen = resultado[0]
                print(f"Imagen recuperada para id {id}, tamaño: {len(imagen)} bytes")  # Añade este print para debugging
                return Response(imagen, mimetype='image/jpeg')
            else:
                print(f"No se encontró imagen para id {id}")  # Añade este print para debugging
                return 'Imagen no encontrada', 404
    except Exception as e:
        print(f"Error al obtener la imagen {id}: {e}")
        return 'Error', 500
    finally:
        conn.close()

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080, threaded=True)

