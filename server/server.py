import json
import time
from datetime import datetime
from flask import Flask, Response, render_template, stream_with_context, request

app = Flask(__name__)

previous_data = None

@app.route("/")
def index():
    return render_template('index.html')

@app.route("/chart-data")
def chart_data():
    def get_data():
        while True:
            if previous_data == None:
                yield f"data:{json.dumps({'time':datetime.now().strftime('%Y-%m-%d %H:%M:%S'), 'value': 0})}\n\n"
            else:
                yield f"data:{previous_data}\n\n"
            time.sleep(1)

    response = Response(stream_with_context(get_data()), mimetype="text/event-stream")
    response.headers["Cache-Control"] = "no-cache"
    response.headers["X-Accel-Buffering"] = "no"
    return response

@app.route("/submit")
def submit():
    global previous_data
    previous_data = json.dumps({'time': datetime.now().strftime('%Y-%m-%d %H:%M:%S'), 'value': request.args.get("temp")})
    return "Temp: " + str(request.args.get("temp"))