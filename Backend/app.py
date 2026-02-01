from flask import Flask, request, jsonify, send_from_directory
import os
from datetime import datetime
import logging

app = Flask(__name__)

UPLOAD_FOLDER = os.getenv('UPLOAD_FOLDER', '/data/photos')
AUTH_USERNAME = os.getenv('AUTH_USERNAME', 'admin')
AUTH_PASSWORD = os.getenv('AUTH_PASSWORD', 'password')
REQUIRE_AUTH = os.getenv('REQUIRE_AUTH', 'true').lower() == 'true'

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

os.makedirs(UPLOAD_FOLDER, exist_ok=True)

def check_auth():
    if not REQUIRE_AUTH:
        return True
    auth = request.authorization
    return auth and auth.username == AUTH_USERNAME and auth.password == AUTH_PASSWORD

@app.route('/health')
def health():
    return jsonify({'status': 'ok', 'timestamp': datetime.now().isoformat()})

@app.route('/upload', methods=['POST'])
def upload():
    if not check_auth():
        return jsonify({'error': 'Unauthorized'}), 401
    
    filename = request.headers.get('X-Filename', f'photo_{datetime.now().strftime("%Y%m%d_%H%M%S")}.jpg')
    filepath = os.path.join(UPLOAD_FOLDER, os.path.basename(filename))
    
    with open(filepath, 'wb') as f:
        f.write(request.data)
    
    logger.info(f"Uploaded: {filename} ({len(request.data)} bytes)")
    return jsonify({'status': 'success', 'filename': filename})

@app.route('/list')
def list_files():
    if not check_auth():
        return jsonify({'error': 'Unauthorized'}), 401
    
    files = []
    for f in os.listdir(UPLOAD_FOLDER):
        if os.path.isfile(os.path.join(UPLOAD_FOLDER, f)):
            stat = os.stat(os.path.join(UPLOAD_FOLDER, f))
            files.append({
                'filename': f,
                'size': stat.st_size,
                'modified': datetime.fromtimestamp(stat.st_mtime).isoformat()
            })
    
    return jsonify({'files': sorted(files, key=lambda x: x['modified'], reverse=True)})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080)