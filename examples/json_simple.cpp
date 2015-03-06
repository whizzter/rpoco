// First very simple example of parsing and writing JSON data

#include <rpoco/rpocojson.hpp>


struct Simple {
	std::string hello;
	int x=0;

	RPOCO(hello,x);
};

int main(int argc,char **argv) {

	// declare structure
	Simple data;

	// sample data
	std::string sampleText="{ \"hello\":\"world\"  ,  \"x\":123  }";

	// parse in data
	rpocojson::parse(sampleText,data);
	
	// now write the data from the object back to a new json string
	std::string outputText=rpocojson::to_json(data);

	// and print it
	printf("%s\n",outputText.c_str());

	return 0;
}
