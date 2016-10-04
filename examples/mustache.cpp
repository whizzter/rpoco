// First very simple example of parsing and writing JSON data

#include <rpoco/rpocojson.hpp>
#include <rpoco/mustache.hpp>

struct Person {
	char name[40];
	int age;
	double loyalty;
	bool child;

	RPOCO(name,age,child,loyalty);
};

struct Store {
	std::string name;
	std::vector<Person> emp;

	RPOCO(name,emp);
};

int main(int argc,char **argv) {

	// declare structure
	Store data;

	// sample data
	std::string sampleText="{ \"name\":\"Acme Store\" , \"emp\":[{\"name\":\"John Doe\",\"age\":12,\"child\":true,\"loyalty\":0.9},{\"name\":\"Jane Doe\",\"age\":34,\"child\":false,\"loyalty\":0.3] }";

	// parse in data
	rpocojson::parse(sampleText,data);

	// template
	std::string tpl="Store:{{name}} {{#emp}}[{{name}} aged {{age}} is a {{#child}}child{{/child}}{{^child}}parent{{/child}} with loyalty {{loyalty}}]{{/emp}}";

	// render template with object data
	auto outputText=rpoco::mustache::parse(tpl).render(data);

	// and print it
	printf("%s",outputText.c_str());

	return 0;
}

