// CGIHandler.cpp
#include "CGIHandler.hpp"
#include "HTTPResponse.hpp"
#include "Server.hpp"
#include <unistd.h>  // For fork, exec, pipe
#include <sys/wait.h>  // For waitpid
#include <signal.h>
#include <iostream>
#include <cstring>  // For strerror
#include "Logger.hpp"
#include "Utils.hpp"
#include <limits.h>

CGIHandler::CGIHandler::CGIHandler(const std::string& scriptPath, const std::string& interpreterPath, const HTTPRequest& request)
    : _scriptPath(scriptPath), _request(request), _interpreterPath(interpreterPath), _pid(-1), _CGIOutput(""), _bytesSent(0), _started(false), _cgiFinished(false), _cgiExitStatus(-1) {
    _outputPipeFd[0] = -1;
    _outputPipeFd[1] = -1;
    _inputPipeFd[0] = -1;
    _inputPipeFd[1] = -1;
    setCGIInput(request.getBody().c_str());
}

CGIHandler::~CGIHandler() {}

// Getters
int CGIHandler::getPid() const { return _pid; }
int CGIHandler::getInputPipeFd() const { return _inputPipeFd[1]; }
int CGIHandler::getOutputPipeFd() const { return _outputPipeFd[0]; }
std::string CGIHandler::getCGIInput() const { return _CGIInput; }
std::string CGIHandler::getCGIOutput() const { return _CGIOutput; }

// Setters
void CGIHandler::setPid(int pid) { _pid = pid; }
void CGIHandler::setInputPipeFd(int inputPipeFd[2]) {
    _inputPipeFd[0] = inputPipeFd[0];
    _inputPipeFd[1] = inputPipeFd[1];
}
void CGIHandler::setOutputPipeFd(int outputPipeFd[2]) {
    _outputPipeFd[0] = outputPipeFd[0];
    _outputPipeFd[1] = outputPipeFd[1];
}
void CGIHandler::setCGIInput(const std::string& CGIInput) { _CGIInput = CGIInput; }
void CGIHandler::setCGIOutput(const std::string& CGIOutput) { _CGIOutput = CGIOutput; }

int CGIHandler::isCgiDone() {
    if (_cgiFinished) {
        // On a déjà appelé waitpid et récupéré le statut
        return _cgiExitStatus;
    }

    int status;
    std::cerr << "NEW PID : " << _pid << std::endl;
    pid_t result = waitpid(_pid, &status, WNOHANG);
    std::cerr << "WAITPID : " << result << std::endl;
    if (result == 0) {
        // Process is still running
        return 0;
    } else if (result == _pid) {
        // Process has exited
        if (WIFEXITED(status)) {
            _cgiExitStatus = WEXITSTATUS(status);
        } else {
            _cgiExitStatus = -1;
        }
        _cgiFinished = true; // On note que c'est fini
        return _cgiExitStatus;
    } else {
        // waitpid failed
        Logger::instance().log(ERROR, "isCGIDone: waitpid failed: " + std::string(to_string(errno)));
        // On évite de rappeler waitpid, on considère que c'est fini mais en erreur
        _cgiFinished = true;
        _cgiExitStatus = -1;
        return _cgiExitStatus;
    }
}



int CGIHandler::writeToCGI() {
    if (_inputPipeFd[1] == -1) {
        return -1;
    }

    const char* bufferPtr = _CGIInput.c_str() + _bytesSent;
    ssize_t remaining = _CGIInput.size() - _bytesSent;

    ssize_t bytesWritten = write(_inputPipeFd[1], bufferPtr, remaining);

    if (bytesWritten > 0) {
        _bytesSent += bytesWritten;
    } else if (bytesWritten == -1) {
        // An error occurred (since poll normally provides from getting EAGAIN error)
        Logger::instance().log(ERROR, "writeToCGI: Write error");
    }

    // Check if all data has been sent
    if (_bytesSent == _CGIInput.size()) {
        // All data sent; close the input pipe
        close(_inputPipeFd[1]);
        _inputPipeFd[1] = -1;
        return 0; // Indicate that writing is complete
    }

    return _bytesSent;
    // Not all data sent yet; continue writing
}

int CGIHandler::readFromCGI() {
    if (_outputPipeFd[0] == -1) {
        return -1;
    }
    char buffer[4096];
    ssize_t bytesRead = read(_outputPipeFd[0], buffer, sizeof(buffer));
    if (bytesRead > 0) {
        _CGIOutput.append(buffer, bytesRead);
    } else if (bytesRead == 0) {
        close(_outputPipeFd[0]);
        _outputPipeFd[0] = -1;
    }
    return bytesRead;
}

bool CGIHandler::hasTimedOut() const {
    unsigned long currentTime = curr_time_ms();
    return (currentTime - _startTime) > CGI_TIMEOUT_MS;
}

void CGIHandler::terminateCGI() {
    if (_pid > 0) {
        kill(_pid, SIGKILL);
        waitpid(_pid, NULL, 0);
        _pid = -1;
    }
    closeInputPipe();
    closeOutputPipe();
}

// Implémentation de la méthode endsWith
bool CGIHandler::endsWith(const std::string& str, const std::string& suffix) const {
    if (str.length() >= suffix.length()) {
        return (0 == str.compare(str.length() - suffix.length(), suffix.length(), suffix));
    } else {
        return false;
    }
}

bool CGIHandler::startCGI() {
    _startTime = curr_time_ms();
    Logger::instance().log(DEBUG, "Startin CGI script: " + _scriptPath);

    std::string interpreter = _interpreterPath;

    Logger::instance().log(DEBUG, "executeCGI: Interpreter = " + interpreter);

    if (pipe(_inputPipeFd) == -1) {
        Logger::instance().log(ERROR, std::string("executeCGI: Input pipe failed: ") + strerror(errno));
        return false;
    }
    if (pipe(_outputPipeFd) == -1) {
        Logger::instance().log(ERROR, std::string("executeCGI: Output pipe failed: ") + strerror(errno));
        return false;
    }

    Logger::instance().log(DEBUG, std::string("pipe fds : INPUT 0 : ") + to_string(_inputPipeFd[0]) + " - INPUT 1 : " + to_string(_inputPipeFd[1]) + " - OUTPUT 0 : " + to_string(_outputPipeFd[0])  + " - OUTPUT 1 : " + to_string(_outputPipeFd[1]));

    int pid = fork();
    if (pid == 0) {
        // Processus enfant : exécution du script CGI
        close(_outputPipeFd[0]);  // Fermer l'extrémité de lecture du pipe pour stdout
        dup2(_outputPipeFd[1], STDOUT_FILENO);  // Rediriger stdout vers le pipe
        close(_outputPipeFd[1]);
        close(_inputPipeFd[1]);  // Fermer l'extrémité d'écriture du pipe pour stdin
        dup2(_inputPipeFd[0], STDIN_FILENO);  // Rediriger stdin vers le pipe
        close(_inputPipeFd[0]);

        setupEnvironment(_request, _scriptPath);

        // Exécuter le script
		// if (chdir())
        execl(interpreter.c_str(), interpreter.c_str(), _scriptPath.c_str(), NULL);
        Logger::instance().log(ERROR, std::string("executeCGI: Failed to execute CGI script: ") + _scriptPath + std::string(". Error: ") + strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid > 0){
        std::cerr << "PID : " << pid << std::endl;
        _pid = pid;
        _started = true;
        close(_outputPipeFd[1]);
        close(_inputPipeFd[0]);
        return true;
    } else if (pid == -1) {
        Logger::instance().log(ERROR, "executeCGI: Fork failed: " + std::string(strerror(errno)));
        // Close all open FDs before returning
        close(_inputPipeFd[0]);
        close(_inputPipeFd[1]);
        close(_outputPipeFd[0]);
        close(_outputPipeFd[1]);
        return false;
    }
    return true;
}

void CGIHandler::setupEnvironment(const HTTPRequest& request, std::string scriptPath) {
    // Déterminer le chemin absolu du script (si nécessaire)
    // S'il est déjà absolu, pas besoin, sinon :
    char absPath[PATH_MAX];
    if (realpath(scriptPath.c_str(), absPath) == NULL) {
        Logger::instance().log(ERROR, "Failed to get absolute path of the CGI script.");
        // Gérer l'erreur si nécessaire
    }

    // Méthode
    setenv("REQUEST_METHOD", request.getMethod().c_str(), 1);

    // Content-Type et Content-Length depuis la requête
    // Ne pas forcer à application/x-www-form-urlencoded, utiliser la valeur réelle :
    std::string contentType = request.getStrHeader("Content-Type");
    if (!contentType.empty()) {
        setenv("CONTENT_TYPE", contentType.c_str(), 1);
    }

    setenv("CONTENT_LENGTH", to_string(request.getBody().size()).c_str(), 1);

    // Variables CGI standard
    setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    setenv("SCRIPT_FILENAME", absPath, 1);  // Utiliser le chemin absolu
    setenv("SCRIPT_NAME", scriptPath.c_str(), 1);
    setenv("QUERY_STRING", request.getQueryString().c_str(), 1);
    setenv("REDIRECT_STATUS", "200", 1);
    setenv("SERVER_PROTOCOL", "HTTP/1.1", 1);

    // Hôte et éventuellement le port
    std::string host = request.getStrHeader("Host");
    setenv("SERVER_NAME", host.c_str(), 1);

    // Ajouter éventuellement :
    setenv("SERVER_SOFTWARE", "webserv/1.0", 1);
    // Si vous connaissez le port du serveur :
    // setenv("SERVER_PORT", "80", 1);

    // Autres variables possibles :
    // setenv("REQUEST_URI", request.getPath().c_str(), 1);
    // setenv("REMOTE_ADDR", request.getClientIP().c_str(), 1); (si disponible)
}


void CGIHandler::closeInputPipe() {
    if (_inputPipeFd[0] != -1) {
        close(_inputPipeFd[0]);
        _inputPipeFd[0] = -1;
    }
    if (_inputPipeFd[1] != -1) {
        close(_inputPipeFd[1]);
        _inputPipeFd[1] = -1;
    }
}

void CGIHandler::closeOutputPipe() {
    if (_outputPipeFd[0] != -1) {
        close(_outputPipeFd[0]);
        _outputPipeFd[0] = -1;
    }
    if (_outputPipeFd[1] != -1) {
        close(_outputPipeFd[1]);
        _outputPipeFd[1] = -1;
    }
}

