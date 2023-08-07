# Pull official base image.
FROM ubuntu:18.04

# Set working directory.
WORKDIR /usr/src/app

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
            python3.8 	  \
						python3-pip   \
            python3.8-dev \
            perl-base		  \
						wget 		\
						bzip2		\
						vim		  \
						sudo    \
            netcat  \
            postgresql-client \
            libpq-dev

# Make sure pip is up to date.
RUN python3.8 -m pip install --upgrade pip

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
