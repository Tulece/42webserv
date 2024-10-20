#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include <map>

class HTTPRequest {
public:
    HTTPRequest();
    ~HTTPRequest();

    std::string getMethod() const;
    std::string getPath() const;
    std::string getQueryString() const;  // Nouvelle méthode
    std::map<std::string, std::string> getHeaders() const;
    std::string getBody() const;

    bool parse(const std::string& raw_request);

private:
    std::string _method;
    std::string _path;
    std::string _queryString;  // Nouveau champ
    std::map<std::string, std::string> _headers;
    std::string _body;

    bool parseRequestLine(const std::string& line);
    void parseHeaders(const std::string& headers);
    void parseBody(const std::string& body);
    void parseQueryString();  // Nouvelle méthode privée
};

#endif
