from flask import Flask, request, jsonify, render_template
from flask_sqlalchemy import SQLAlchemy
from datetime import datetime
import pandas as pd

app = Flask(__name__)
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///sensors.db'
db = SQLAlchemy(app)

class Reading(db.Model):
    id          = db.Column(db.Integer, primary_key=True)
    timestamp   = db.Column(db.DateTime, default=datetime.utcnow)
    temperature = db.Column(db.Float)
    humidity    = db.Column(db.Float)
    pm25        = db.Column(db.Float)
    pm10        = db.Column(db.Float)
    gas_ppm     = db.Column(db.Float)

with app.app_context():
    db.create_all()

@app.route('/api/data', methods=['POST'])
def receive_data():
    d = request.json
    db.session.add(Reading(**d))
    db.session.commit()
    return jsonify({'status': 'ok'})

@app.route('/api/history')
def history():
    rows = Reading.query.order_by(Reading.timestamp.desc()).limit(200).all()
    return jsonify([{
        'time': r.timestamp.isoformat(),
        'temperature': r.temperature,
        'humidity': r.humidity,
        'pm25': r.pm25,
        'pm10': r.pm10,
        'gas_ppm': r.gas_ppm
    } for r in rows])

@app.route('/api/analysis')
def analysis():
    rows = Reading.query.order_by(Reading.timestamp.desc()).limit(500).all()
    df = pd.DataFrame([{
        'temperature': r.temperature, 'humidity': r.humidity,
        'pm25': r.pm25, 'pm10': r.pm10, 'gas_ppm': r.gas_ppm
    } for r in rows])

    if df.empty:
        return jsonify({})

    # Air quality classification
    def aqi(pm):
        if pm < 0.012:   return 'Good'
        elif pm < 0.035: return 'Moderate'
        elif pm < 0.055: return 'Unhealthy for sensitive'
        else:            return 'Unhealthy'

    return jsonify({
        'avg':         df.mean().round(3).to_dict(),
        'max':         df.max().round(3).to_dict(),
        'min':         df.min().round(3).to_dict(),
        'aqi':         aqi(df['pm25'].mean()),
        'correlation': {
            'pm25_vs_humidity': round(df['pm25'].corr(df['humidity']), 3),
            'pm25_vs_gas':      round(df['pm25'].corr(df['gas_ppm']), 3),
            'temp_vs_humidity': round(df['temperature'].corr(df['humidity']), 3),
        },
        'anomalies': {
            'high_pm25':   int((df['pm25'] > 0.05).sum()),
            'gas_events':  int((df['gas_ppm'] > 1000).sum()),
        }
    })

@app.route('/')
def dashboard():
    return render_template('dashboard.html')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
