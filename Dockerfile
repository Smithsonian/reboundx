# Pull official base image.
FROM python:3.11

# Set environment variables.
# PYTHONDONTWRITEBYTECODE: Prevents Python from writing pyc files to disc (equivalent to python -B option).
ENV PYTHONDONTWRITEBYTECODE 1
# For local development, use 0
ENV USE_CHEBY_CHECKER 1

# PYTHONUNBUFFERED: Prevents Python from buffering stdout and stderr (equivalent to python -u option).
ENV PYTHONUNBUFFERED 1

# install system dependencies
RUN apt-get -y update && apt-get install -y \
          	bash    \
            gcc  		\
            make 		\
            git     \
            git-lfs \
            perl-base		  \
						wget 		\
						bzip2		\
						vim		  \
						sudo    \
            netcat-traditional  \
            postgresql-client \
            libpq-dev


# --- REBOUND INSTALLS ----------
WORKDIR /
RUN git clone https://github.com/Smithsonian/rebound.git
WORKDIR /rebound
RUN make
ENV REB_DIR=/rebound
ENV LD_LIBRARY_PATH=/rebound/src

WORKDIR /
COPY . /reboundx
WORKDIR /reboundx/examples/ephem_forces/
RUN make

# Install python dependencies
RUN pip install --upgrade pip
COPY ./requirements.txt /base_rebound_image/requirements.txt
RUN pip install -r /base_rebound_image/requirements.txt
