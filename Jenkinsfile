pipeline {
    agent any
    stages {
        stage('checkout pr') {
            steps {
                //cleanup
                cleanWs()
                checkout scm
            }
        }
        stage('meta module build') {
            steps {
                sh '''
                source /opt/rh/devtoolset-10/enable
                cd meta
                cmake .
                cmake --build .
                cd ../meta_example
                mkdir build
                cd build
                cmake ..
                make
                '''
            }
        }
        stage('meta test') {
            steps {
                // 执行 meta_test.sh 脚本
                sh '''
                sh src/test/regress/intple/class5/meta_test.sh
                '''
            }
        }
        stage('build') {
            steps {
                sh '''
                rm -rf tmp_build
                sudo yum install -y git glibc-devel libaio-devel flex bison ncurses-devel patch lsb_release readline-devel ndctl-devel daxctl-devel numactl-devel ipmctl rdma-core-devel
                sudo yum install -y libaio-devel flex ncurses-devel pam-devel libffi-devel autoconf automake cmake openssl-devel libtool libtool-devel make readline readline-devel python python3-devel bison tbb ndctl daxctl ndctl-devel daxctl-devel
                mkdir tmp_build
                sh build.sh -m release -3rd /var/jenkins_home/binarylibs/ -pkg
                tar -zcvf mppdb_temp_install.tar.gz mppdb_temp_install
                '''
            }
        }
        stage('openGauss test') {
            steps {
                //fastcheck
                sh '''
                sed -i 's/declare CMAKE_PKG="Y"/declare CMAKE_PKG="N"/' build/script/build_opengauss.sh
                sh build.sh -m release -3rd /var/jenkins_home/binarylibs/ -pkg
                export CODE_BASE=$PWD   
                export BINARYLIBS=/var/jenkins_home/binarylibs   
                export GAUSSHOME=$CODE_BASE/mppdb_temp_install
                export GCC_PATH=$BINARYLIBS/buildtools/gcc10.3
                export CC=$GCC_PATH/gcc/bin/gcc
                export CXX=$GCC_PATH/gcc/bin/g++
                export LD_LIBRARY_PATH=$GAUSSHOME/lib:$GCC_PATH/gcc/lib64:$GCC_PATH/isl/lib:$GCC_PATH/mpc/lib/:$GCC_PATH/mpfr/lib/:$GCC_PATH/gmp/lib/:$LD_LIBRARY_PATH
                export PATH=$GAUSSHOME/bin:$GCC_PATH/gcc/bin:$PATH
                cd mppdb_temp_install/
                export DATA_PATH=$PWD
                cd bin/
                gs_initdb -D $DATA_PATH/data/ --nodename=node1
                sed -i 's/#port = 5432/port = 12400/' "$DATA_PATH/data/postgresql.conf"
                cd ../../src/test/regress
                rm -rf /mnt/pmem0/test_folder 2>/dev/null
                rm -rf /mnt/pmem1/test_folder 2>/dev/null
                make fastcheck_single part=A
                '''
            }
        }
        stage('unit test') {
            steps {
                //sh 'make fastcheck_single PART=E'
                sh 'echo unit test;'
            }
        }
        stage('coding check') {
            steps {
                //todo
                sh 'echo "coding check";'
            }
        }
    }
    post {
        always {
            echo 'Finish!'
        }
        success {
            echo 'Success'
        }
    }
}