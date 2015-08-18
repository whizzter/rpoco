// Minimal loader for tiled's json file format in unencoded format (store as CSV)

// Notice: This loader is NOT complete and files must be correctly saved
//         but for quick and small projects this will work.
//         In tiled make sure that the layer format is specified as CSV when
//         viewing the Map->Map properties.. menu.

#include <fstream>
#include <rpoco/rpocojson.hpp>

// Only handles square objects for the time being
// Notice: x/y/width/height are specified in pixels and not tiles!
struct tiled_object {
	std::string type; // user specified
	std::string name; // user specified
	int x;
	int y;
	int width;
	int height;
	RPOCO(type,name,x,y,width,height);
};

// Could be object layer or tilelayer
// x/y/width/height is specified in tiles
struct tiled_layer {
	std::string type; // "objectgroup" or "tilelayer"
	std::string name; // user specified
	int x;
	int y;
	int width;
	int height;
	std::vector<int> data; // valid for "tilelayer"'s saved as CSV
	std::vector<tiled_object> objects; // valid for "objectgroup"'s

	RPOCO(type,x,y,width,height,name,data,objects);
};

// Stores info about the images used for the tilemap
// the tile id's (referenced from tiled_layer::"tilelayer"'s data)
// are in the range: firstgid-(firstgid+range)
// where range is (imagewidth*imageheight)/(tilewidth*tileheight)
// tile's are stored horizontally first
struct tiled_tileset {
	std::string name;
	std::string image;

	int firstgid; // the first ID, use this as a base to calculate what tiles a layer refers to
	int imagewidth;
	int imageheight;
	int tilewidth;
	int tileheight;
	RPOCO(name,image,firstgid,imagewidth,imageheight,tilewidth,tileheight);
};

// The root file structure used for parsing,etc.
struct tiled_file {
	int width,height;
	int tilewidth,tileheight;
	std::vector<tiled_layer> layers;
	std::vector<tiled_tileset> tilesets;

	RPOCO(width,height,tilewidth,tileheight,layers,tilesets);
};

int main(int argc,char **argv) {
	tiled_file tfile;
	if (argc<2) {
		std::cout<<"no filename specified\n";
		return -1;
	}
	std::ifstream is(argv[1]);
	if (rpocojson::parse(is,tfile)) {
		// insert code here to do something useful with the tiledata :)
		std::cout<<"the file "<<argv[1]<<" was correctly parsed\n";
		return 0;
	} else {
		std::cout<<"could not parse "<<argv[1]<<"\n";
		return -1;
	}
}
