FROM python:3.11-slim

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY metrics_payload.py sender_v2.py ./

ENV PYTHONUNBUFFERED=1

CMD ["python", "sender_v2.py"]
