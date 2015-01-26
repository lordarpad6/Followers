#include "Level.h"

#include <fstream>
#include <limits>

#include "Engine/Errors.h"
#include "Engine/ResourceMngr.h"
#include <stddef.h>

#define kMERIDIAN_LENGTH_METERS   20003930.0
#define kMERIDIAN_LENGTH_WGS_UNITS   ( 180.0 * 100000 )
#define kWGS_UNIT_TO_METER   ( kMERIDIAN_LENGTH_METERS / kMERIDIAN_LENGTH_WGS_UNITS )
#define kMETER_TO_WGS_UNIT   ( 1.0 / kWGS_UNIT_TO_METER )

const int ROW = 256;
const int COL = 256;
const double CELL_SIZE = 10;

Level::Level(const std::string &fileName, Engine::GLSLProgram *shaderProgram) : shaderProgram(nullptr), nCols(0), nRows(0), levelData(nullptr)
{
	this->shaderProgram = shaderProgram;

	//additional initialization
	vboId = iboId = vboIdWireframe = iboIdWireframe = 0;
	showWireframe = false;

	//read binary data from file
	readBinaryData(fileName);

	//convert meters to WGS units
	for(int z=0; z<nRows; z++)
		for (int x=0; x<nCols; x++)
			levelData[z*nCols + x]*=kMETER_TO_WGS_UNIT;

	//build color height
	heightColorMap.insert(std::make_pair(0.0f, Engine::ColorRGBA8()));
	heightColorMap.insert(std::make_pair(1100.0f, Engine::ColorRGBA8(0,150,150)));
	heightColorMap.insert(std::make_pair(1500.0f, Engine::ColorRGBA8(0,180,0)));
	heightColorMap.insert(std::make_pair(1700.0f, Engine::ColorRGBA8(255,255,0)));
	heightColorMap.insert(std::make_pair(1900.0f, Engine::ColorRGBA8(255,120,0)));
	heightColorMap.insert(std::make_pair(2100.0f, Engine::ColorRGBA8(180,31,21)));
	heightColorMap.insert(std::make_pair(2300.0f, Engine::ColorRGBA8(128,60,60)));
	heightColorMap.insert(std::make_pair(2600.0f, Engine::ColorRGBA8(255,255,255)));
	heightColorMap.insert(std::make_pair(4000.0f, Engine::ColorRGBA8(255,255,255,100)));

	//construct vertex data based on level data
	Engine::Vertex *vertexData = new Engine::Vertex[ ROW * COL ];
	double cosMeridian = cos(yllcorner*3.14/180);
	for(int z=0; z<ROW; z++)
		for (int x=0; x<COL; x++)
		{
			float y = levelData[(z)*nCols + x];
			vertexData[z*COL + x].SetPosition(x*CELL_SIZE*cosMeridian, y, z*CELL_SIZE);
			vertexData[z*COL + x].color = getColorByHeight(y);
			vertexData[z*COL + x].SetUV(x%2,z%2);
		}

	//calculate vertex normals based on triangles formed with adjacent vertexes
	//interior
	for(int z=1; z<ROW-1; z++)
		for (int x=1; x<COL-1; x++)
		{
			Engine::Position leftUpper = normal(vertexData[z*ROW + x].position, vertexData[(z-1)*ROW + x].position, vertexData[z*ROW + x-1].position);
			Engine::Position centerUpper = normal(vertexData[z*ROW + x].position, vertexData[(z-1)*ROW + x+1].position, vertexData[(z-1)*ROW + x].position);
			Engine::Position rightUpper = normal(vertexData[z*ROW + x].position, vertexData[z*ROW + x+1].position, vertexData[(z-1)*ROW + x+1].position);
			Engine::Position rightLower = normal(vertexData[z*ROW + x].position, vertexData[(z+1)*ROW + x].position, vertexData[z*ROW + x+1].position);
			Engine::Position centerLower = normal(vertexData[z*ROW + x].position, vertexData[(z+1)*ROW + x-1].position, vertexData[(z+1)*ROW + x].position);
			Engine::Position leftLower = normal(vertexData[z*ROW + x].position, vertexData[z*ROW + x-1].position, vertexData[(z+1)*ROW + x-1].position);
			//Engine::Position leftLower = normal(vertexData[z*ROW + x].position, vertexData[z*ROW + x-1].position, vertexData[(z+1)*ROW + x].position);
			//Engine::Position rightUpper = normal(vertexData[z*ROW + x].position, vertexData[z*ROW + x+1].position, vertexData[(z-1)*ROW + x].position);
			vertexData[z*ROW + x].normal = (leftUpper + centerUpper + rightUpper + rightLower + centerLower + leftLower);
			vertexData[z*ROW + x].normal.Normalize();
		}
	//TODO corners
	//first and last row
	//first and last column
	//upload to GPU
	glGenBuffers(1, &vboId);
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Engine::Vertex)*ROW * COL, vertexData,GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color for wire frame vertex, and upload to GPU
	Engine::ColorRGBA8 wireframeColor(0,0,0,255);
	for(int z=0; z<ROW; z++)
		for (int x=0; x<COL; x++)
			vertexData[z*COL + x].color = wireframeColor;
	glGenBuffers(1, &vboIdWireframe);
	glBindBuffer(GL_ARRAY_BUFFER, vboIdWireframe);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Engine::Vertex)*ROW * COL,vertexData, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//upload data for normals
	Engine::Vertex *normalsData = new Engine::Vertex[ROW * COL *2];
	for(int z=0,i=0; z<ROW; z++)
		for (int x=0; x<COL; x++)
		{
			normalsData[i++].position = vertexData[z*COL +x].position;
			normalsData[i++].position = vertexData[z*COL +x].position + vertexData[z*COL +x].normal*5;
		}
	glGenBuffers(1, &vboNormals);
	glBindBuffer(GL_ARRAY_BUFFER, vboNormals);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Engine::Vertex)*ROW * COL*2,normalsData, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	delete[] normalsData;
	delete[] vertexData;

	//assign triangle indices. Two triangles at once, that form a rectangle.
	unsigned int *mapElements = new unsigned int[(COL-1)*(ROW-1)*2*3];
	for(int i=0,k=0; i<ROW-1; i++)
		for(int j=0; j<COL-1; j++)
		{
			mapElements[k++] = i*COL + j;
			mapElements[k++] = (i+1)*COL + j;
			mapElements[k++] = i*COL + j+1;

 			mapElements[k++] = i*COL + j+1;
 			mapElements[k++] = (i+1)*COL + j;
 			mapElements[k++] = (i+1)*COL + j+1;
		}
	glGenBuffers(1, &iboId);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboId);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int)*(COL-1)*(ROW-1)*2*3, mapElements, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	delete[] mapElements;

	//assign line indices.
	unsigned int *wireframeElements =new unsigned int[(ROW*(COL-1)+COL*(ROW-1)+(ROW-1)*(COL-1))*2];
	int k=0;
	//horizontal
	for(int i=0; i<ROW; i++)
		for(int j=0; j<COL-1; j++)
		{
			wireframeElements[k++] = i*COL + j;
			wireframeElements[k++] = i*COL + j+1;
		}
	//vertical
	for(int j=0; j<COL; j++)
		for(int i=0; i<ROW-1; i++)
		{
			wireframeElements[k++] = i*COL + j;
			wireframeElements[k++] = (i+1)*COL + j;
		}
	//diagonal
	for(int i=0; i<ROW-1; i++)
		for(int j=0; j<COL-1; j++)
		{
			wireframeElements[k++] = i*COL + j+1;
			wireframeElements[k++] = (i+1)*COL + j;
		}
	glGenBuffers(1, &iboIdWireframe);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboIdWireframe);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int)*(ROW*(COL-1)+COL*(ROW-1)+(ROW-1)*(COL-1))*2, wireframeElements, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	delete[] wireframeElements;

	//generate and upload height map to shader
	float multiplier;	
	unsigned short *img = makeHeightMap(multiplier);
	glGenTextures(1, &heightMapId);

	glBindTexture(GL_TEXTURE_2D, heightMapId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, COL, ROW, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, img);

	glBindTexture(GL_TEXTURE_2D, 0);

	shaderProgram->Use();
	//upload heightMap uniform
	GLint heightMapLocation = shaderProgram->GetUniformLocation("heightMap");
	glUniform1i(heightMapLocation, 7);

	//upload multiplier, used to decompress heights
	GLint multiplyerLocation = shaderProgram->GetUniformLocation("multiplier");
	glUniform1f(multiplyerLocation, multiplier);

	//upload multipliers, used to skew world coordinates to texture coordinates
	GLint rowMultiplyerLocation = shaderProgram->GetUniformLocation("rowMultiplier");
	glUniform1f(rowMultiplyerLocation, 1/(ROW*CELL_SIZE));
	GLint colMultiplyerLocation = shaderProgram->GetUniformLocation("colMultiplier");
	glUniform1f(colMultiplyerLocation, 1/(COL*CELL_SIZE*cosMeridian));

	shaderProgram->UnUse();

	printf("Uploaded map!\n");
}

Level::~Level(void)
{
	delete[] levelData;
}

void Level::SwitchWireframeVisibility()
{
	showWireframe = !showWireframe;
}

void Level::Render()
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D,Engine::ResourceMngr::GetTexture("Textures/PNG/ground.png").id);

 	glActiveTexture(GL_TEXTURE7);
 	glBindTexture(GL_TEXTURE_2D,heightMapId);

	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(0, 3, GL_FLOAT ,GL_FALSE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,position));
	glVertexAttribPointer(1, 3, GL_FLOAT ,GL_FALSE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,normal));
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,color));
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,uv));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboId);
	int size;  glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
	glDrawElements(GL_TRIANGLES, size/sizeof(unsigned int), GL_UNSIGNED_INT, 0);

	if(showWireframe)
	{
		//draw wire frame
		glPolygonOffset(1, 1);
		glEnable(GL_POLYGON_OFFSET_FILL);

		glBindBuffer(GL_ARRAY_BUFFER, vboIdWireframe);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(0, 3, GL_FLOAT ,GL_FALSE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,position));
		glVertexAttribPointer(1, 3, GL_FLOAT ,GL_FALSE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,normal));
		glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,color));
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,uv));

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboIdWireframe);
		int size;  glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
		glDrawElements(GL_LINES, size/sizeof(unsigned int), GL_UNSIGNED_INT, 0);

		//draw normals
		glBindBuffer(GL_ARRAY_BUFFER, vboNormals);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(0, 3, GL_FLOAT ,GL_FALSE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,position));
		glVertexAttribPointer(1, 3, GL_FLOAT ,GL_FALSE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,normal));
		glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,color));
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Engine::Vertex), (void*)offsetof(Engine::Vertex,uv));

		glDrawArrays(GL_LINES, 0, ROW * COL * 2);
	}

	//glDrawArrays(GL_TRIANGLES, 0, /*(ROW-1) **/ (COL-2));

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D,0);
}

double Level::GetHeight(glm::vec2 point2d)
{
	double cosMeridian = cos(yllcorner*3.14/180);

	int posx1 = (int)(point2d.x / CELL_SIZE / cosMeridian);
	int posx2 = posx1 + 1;
	int posy1 = (int)(point2d.y / CELL_SIZE);
	int posy2 = posy1 + 1;

	double f00 = levelData[posy1*nCols + posx1];
	double f10 = levelData[posy1*nCols + posx2];
	double f11 = levelData[posy2*nCols + posx2];
	double f01 = levelData[posy2*nCols + posx1];

	double px = point2d.x / CELL_SIZE /cosMeridian - posx1;
	double py = point2d.y / CELL_SIZE - posy1;

	double fFinal = f00 * (1.0 - px) * (1.0 - py) +
		f10 * px * (1.0 - py) + f01 * (1.0 - px) * py + f11 * px * py;

	return fFinal;
}

Engine::ColorRGBA8 Level::getColorByHeight(float height)
{
	//Get the first element that is greater than or equal with height. This will be the upper bound. Seriously.
	auto itLow = heightColorMap.lower_bound(height);
	//Now make the upper bound equal to lower bound(witch is still the upper bound), then decrement the upper bound.
	auto itUp = itLow--;

	float height1=itLow->first, height2=itUp->first;
	Engine::ColorRGBA8 c1=itLow->second, c2=itUp->second, cRes;
	float t = (height - height1) / (height2 - height1);
	cRes.r = (c2.r - c1.r) * t + c1.r;
	cRes.g = (c2.g - c1.g) * t + c1.g;
	cRes.b = (c2.b - c1.b) * t + c1.b;
	cRes.a = (c2.a - c1.a) * t + c1.a;
	return cRes;
}
Engine::Position Level::normal(Engine::Position p1, Engine::Position p2, Engine::Position p3)
{
	Engine::Position a, b;
	// calculate the vectors A and B
	// note that p[3] is defined with counterclockwise winding in mind
	a = p1 - p2; 
	b = p2 - p3; 

	// calculate the cross product
	Engine::Position normal;
	normal.x = (a.y * b.z) - (a.z * b.y); 
	normal.y = (a.z * b.x) - (a.x * b.z); 
	normal.z = (a.x * b.y) - (a.y * b.x); 

	return normal;
}

void Level::getMaxMinHeight(float &maxHeight, float &minHeight)
{
	maxHeight = INT_MIN;
	minHeight = INT_MAX;
	for (int i=0; i<nRows; i++)
		for(int j=0; j<nCols; j++)
		{
			float x = levelData[i*nCols + j];
			if(x<minHeight)
				minHeight = x;
			if(x>maxHeight)
				maxHeight = x;
		}
}
unsigned short* Level::makeHeightMap(float &multiplier)
{
	float maxH,minH;
	getMaxMinHeight(maxH,minH);
	multiplier = USHRT_MAX / (maxH-minH); 

	unsigned short* img = new unsigned short[ROW*COL];
	for (int i=0; i<ROW; i++)
		for(int j=0; j<COL; j++)
			img[i*COL + j] = 0;// levelData[i*nCols + j] * multiplier;
	return img;
}

void Level::readAscFile(const std::string &fileName)
{
	std::ifstream file(fileName);
	if(file.fail())
		Engine::fatalError("File could not be opened!");

	std::string temp; ///temporary string

	file>>temp>>nCols; ///ignore word and read nCols
	file>>temp>>nRows; ///ignore word and read nRows
	file>>temp>>xllcorner; ///ignore word and read xllcorner
	file>>temp>>yllcorner; ///ignore word and read yllcorner
	file>>temp>>cellsize; ///ignore word and read cellsize
	file>>temp>>noData_value; ///ignore word and read NODATA_value

	//allocate memory
	levelData = new float[nRows*nCols];
	//read actual matrix data
	for (int y=0; y<nRows; ++y)
	{
		const int FINISHED_ELEMENTS = y*nCols;
		for (int x=0; x<nCols; ++x)
			file>>levelData[FINISHED_ELEMENTS + x];
	}
}
void Level::writeToBinary(const std::string &fileName, unsigned int nCols, unsigned int nRows)
{
	std::ofstream file(fileName,std::ios::binary);
	if(file.fail())
		Engine::fatalError("File could not be opened!");
	if(!nCols) nCols = this->nCols;
	if(!nRows) nRows = this->nRows;

	file.write((char*)&nCols,sizeof(nCols));
	file.write((char*)&nRows,sizeof(nRows));
	file.write((char*)&xllcorner,sizeof(xllcorner));
	file.write((char*)&yllcorner,sizeof(yllcorner));
	file.write((char*)&cellsize,sizeof(cellsize));
	file.write((char*)&noData_value,sizeof(noData_value));

	for(int i=0; i<nRows; i++)
		file.write((char*)&levelData[i*this->nCols],sizeof(float)*nCols);
}
void Level::readBinaryData(const std::string &fileName)
{
	//data from file
	std::ifstream file(fileName,std::ios::binary);
	if(file.fail())
		Engine::fatalError("File could not be opened!");
	file.read((char*)&nCols,sizeof(nCols));
	file.read((char*)&nRows,sizeof(nRows));
	file.read((char*)&xllcorner,sizeof(xllcorner));
	file.read((char*)&yllcorner,sizeof(yllcorner));
	file.read((char*)&cellsize,sizeof(cellsize));
	file.read((char*)&noData_value,sizeof(noData_value));

	levelData = new float[nRows*nCols];
	file.read((char*)levelData,sizeof(float)*nRows*nCols);
}
