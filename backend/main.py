import os
from dotenv import load_dotenv
from flask import Flask
from flask_cors import CORS
from flask_jwt_extended import JWTManager, jwt_required
from utilities.auth_helpers import signup, login, refresh, logout
from utilities.device_helpers import provision_device, activate_device, get_user_devices

load_dotenv()

app = Flask(__name__)
app.config["JWT_SECRET_KEY"] = os.getenv("JWT_SECRET_KEY")
app.config["JWT_ALGORITHM"] = "HS256"
jwt = JWTManager(app)
CORS(app, resources={r"/api/*": {"origins": "*"}})

# --- AUTHENTICATION ROUTE ---
@app.route('/api/auth/signup', methods=['POST'])
def signup_route():
    return signup()

@app.route('/api/auth/login', methods=['POST'])
def login_route():
    return login()

@app.route('/api/auth/refresh', methods=['POST'])
@jwt_required(refresh=True)
def refresh_route():
    return refresh()

@app.route('/api/auth/logout', methods=['POST'])
@jwt_required(refresh=True)
def logout_route():
    return logout()

# --- DEVICE PROVISIONING ROUTES ---
@app.route('/api/devices/provision', methods=['POST'])
@jwt_required()
def provision_device_route():
    return provision_device()

@app.route('/api/devices/activate', methods=["POST"])
def activate_device_route():
    return activate_device()

@app.route('/api/devices/by_user', methods=['GET'])
@jwt_required()
def get_user_devices_route():
    return get_user_devices()

if __name__ == '__main__':
    app.run(host="0.0.0.0", port=5000)


