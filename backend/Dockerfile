# Setting up base image
FROM python:3.11-slim
# Selecting working directory. Rest of the commands will be executed in this directory
WORKDIR /app
# Installing dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
# Copying project files
COPY . .
# final command to run the application
CMD ["python", "main.py"]

