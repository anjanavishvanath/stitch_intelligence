# Setting up base image
FROM python:3.11-slim
# Selecting working directory. Rest of the commands will be executed in this directory
WORKDIR /app
# System deps (psycopg2-binary needs libpq runtime)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    && rm -rf /var/lib/apt/lists/*
# Installing dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
# Copying project files
COPY . .
# Expose Flask port
EXPOSE 5000
# Production: gunicorn with 2 workers (override CMD locally if you want python main.py for debugging)
CMD ["gunicorn", "--bind", "0.0.0.0:5000", "--workers", "2", "--timeout", "60", "--access-logfile", "-", "--error-logfile", "-", "main:app"]

