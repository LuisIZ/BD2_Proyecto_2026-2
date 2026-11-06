# API y motor: compila el binario motor_sql y sirve FastAPI en el puerto 8000
FROM python:3.14-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make libgl1 libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY api/requirements.txt api/requirements.txt
RUN pip install --no-cache-dir -r api/requirements.txt

COPY Makefile ./
COPY motor motor
RUN make motor

COPY api api

ENV BD2_DATOS=/app/datos \
    BD2_DB=.build/db

EXPOSE 8000
CMD ["python", "-m", "uvicorn", "api.web_api:app", "--host", "0.0.0.0", "--port", "8000"]
