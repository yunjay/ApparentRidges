#include <vector>
#include <array>
#include <iostream>
#include <string>
#include <fstream>
#include <chrono>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include <assimp/Importer.hpp>      
#include <assimp/scene.h>          
#include <assimp/postprocess.h>     

#include "LoadShader.h"
const unsigned int workGroupSize = 1024;
bool loadAssimp(const char* path,std::vector<glm::vec3>& out_vertices,std::vector<glm::vec3>& out_normals,std::vector<unsigned int>& out_indices);
void printVec(glm::vec3 v) {
	std::cout <<"(" << v.x << ", " << v.y << ", " << v.z << ") ";
}
void printVec(glm::vec4 v) {
	std::cout <<"(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ") ";
}

class Model {
public:
	//Handles
	//Buffers
	GLuint VAO, positionBuffer, normalBuffer, textureBuffer, EBO;
	GLuint PDBuffer, CurvatureBuffer;
	GLuint maxPDVBO, maxCurvVBO, minPDVBO, minCurvVBO;
	GLuint vertexStorageBuffer, normalStorageBuffer, indexStorageBuffer; 
	//q1 : max view-dep curvature, t1 : max view-dep curvature direction
	//Dt1q1 : max view-dependent curvature's directional derivative in direction t1
	//TODO : These should be static
	GLuint adjacentFacesBuffer, q1Buffer, t1Buffer, Dt1q1Buffer;
	GLuint pointAreaBuffer, cornerAreaBuffer;

	//shaders
	GLuint viewDepCurvatureCompute, Dt1q1Compute, pointAreaCompute;

	std::vector<glm::vec3> vertices;
	std::vector<glm::vec3> normals;
	std::vector<std::array<unsigned int,3>> faces;
	std::vector<glm::vec2> textureCoordinates;
	std::vector<glm::vec3> tangents;
	std::vector<glm::vec3> bitangents;
	std::vector<GLuint> indices;

	std::vector<glm::vec4> PDs;
	std::vector<GLfloat> PrincipalCurvatures;
	std::vector<std::array<int, 20>> adjacentFaces; //10 pairs of indices of adjacent edges to the vertex
	std::vector<GLfloat> pointAreas; //for every vertex 
	std::vector<GLfloat> cornerAreas; //for every index 

	std::vector<glm::vec4> maxPDs;
	std::vector<glm::vec4> minPDs;
	std::vector<GLfloat> maxCurvs;
	std::vector<GLfloat> minCurvs;

	std::vector<float> q1s;
	std::vector<glm::vec2> t1s;
	std::vector<float> Dt1q1s;

	unsigned int numVertices;
	unsigned int numNormals;
	unsigned int numFaces;
	unsigned int numIndices;

	std::string path;
	GLfloat diagonalLength = 0.0f;
	GLfloat modelScaleFactor = 1.0f;
	GLfloat minDistance;
	glm::vec3 center = glm::vec3(0.0f);
	GLuint size;
	bool isSet = false;
	bool curvaturesCalculated = false;
	bool apparentRidges = false;
	bool printed = false;

	//Debugging area
	glm::mat4 modelMatrix;

	Model(std::string path) {
		this->path = path;
		if (!this->loadAssimp()) { std::cout << "Model at "<<path<<" not loaded!\n"; };
		this->boundingBox();
		this->minDistance = this->getMinDistance();
		this->size = this->vertices.size();
		this->computeCurvatures(); 
		this->findAdjacentFaces();
		this->setup();
	}
	void setup() {
		//std::cout << "Setting up buffers.\n";
		//vector.data() == &vector[0]
		glGenVertexArrays(1, &VAO); //vertex array object
		glGenBuffers(1, &positionBuffer); //vertex buffer object
		glGenBuffers(1, &normalBuffer); //vertex buffer object
		glGenBuffers(1, &textureBuffer); //vertex buffer object

		glGenBuffers(1, &EBO);

		//glGenBuffers(1, &PDBuffer);
		//glGenBuffers(1, &CurvBuffer);

		//VAO  
		glBindVertexArray(VAO);

		glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);
		glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(glm::vec3), normals.data(), GL_STATIC_DRAW);

		//EBO
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);



		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);
		// glVertexAttribPointer(index, size, type, normalized(bool), stride(byte offset between), pointer(offset))
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);

		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);

		glEnableVertexAttribArray(2);
		glBindBuffer(GL_ARRAY_BUFFER, textureBuffer);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);

		// Principal Directions / Principal Curvatures (As VBOs)
		if(this->curvaturesCalculated){
			glGenBuffers(1, &maxPDVBO); //3
			glGenBuffers(1, &minPDVBO); //4
			glGenBuffers(1, &maxCurvVBO); //5
			glGenBuffers(1, &minCurvVBO); //6

			//Get data from SSBOs
			//void glGetNamedBufferSubData(	GLuint buffer,GLintptr offset,GLsizeiptr size,void *data);
			//buffer -> handle, offset -> start of data to read, size -> size of data to be read, *data -> pointer to return data

			glGetNamedBufferSubData(PDBuffer, 0, PDs.size() * sizeof(glm::vec4), PDs.data());
			glGetNamedBufferSubData(CurvatureBuffer,0,PrincipalCurvatures.size()*sizeof(GLfloat),PrincipalCurvatures.data());
		
			for (int dbg = 0; dbg < 2; dbg++) {
				std::cout << "PrincipalCurvatures["<< dbg <<"] " << PrincipalCurvatures[dbg] << "\n";
				std::cout << "PrincipalCurvatures["<< vertices.size() + dbg <<"] " << PrincipalCurvatures[vertices.size() + dbg] << "\n";
			}
			

			//Send PDs / PCs as attrbutes per vertex, just to uncomplicate some of this process.
			glBindBuffer(GL_ARRAY_BUFFER,maxPDVBO);
			glBufferData(GL_ARRAY_BUFFER,PDs.size()/2*sizeof(glm::vec4),PDs.data(),GL_STATIC_DRAW);
			glEnableVertexAttribArray(3);
			glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,0,(void*)0); //Our PDs are vec4s due to SSBO reasons.

			glBindBuffer(GL_ARRAY_BUFFER,minPDVBO);
			glBufferData(GL_ARRAY_BUFFER,PDs.size()/2*sizeof(glm::vec4),&PDs[this->vertices.size()],GL_STATIC_DRAW);
			glEnableVertexAttribArray(4);
			glVertexAttribPointer(4,4,GL_FLOAT,GL_FALSE,0,(void*)0);

			glBindBuffer(GL_ARRAY_BUFFER,maxCurvVBO);
			glBufferData(GL_ARRAY_BUFFER,PrincipalCurvatures.size()/2*sizeof(GLfloat),PrincipalCurvatures.data(),GL_STATIC_DRAW);
			glEnableVertexAttribArray(5);
			glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE,0,(void*)0);

			glBindBuffer(GL_ARRAY_BUFFER,minCurvVBO);
			glBufferData(GL_ARRAY_BUFFER,PrincipalCurvatures.size()/2*sizeof(GLfloat),&PrincipalCurvatures[this->vertices.size()],GL_STATIC_DRAW);
			glEnableVertexAttribArray(6);
			glVertexAttribPointer(6,1,GL_FLOAT,GL_FALSE,0,(void*)0);


			//glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), &vertices[0], GL_STATIC_DRAW);

		}
		else { this->computeCurvatures(); this->setup(); }

		//adjacent faces
		
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 20, adjacentFacesBuffer);
		glGetNamedBufferSubData(adjacentFacesBuffer, 0, adjacentFaces.size() * sizeof(glm::vec4), adjacentFaces.data());
		for (int dbg = 0; dbg < 1; dbg++) {
			std::cout << "Adjacent to " << dbg<<" : ";
			for (int j = 0; j < 10; j++) {
				std::cout << adjacentFaces[dbg][2 * j] << ", " << adjacentFaces[dbg][2 * j + 1] << ". ";
			}
			std::cout << "\n";
		}

		q1s.resize(this->numVertices,0.0f);
		glGenBuffers(1, &q1Buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, q1Buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, q1s.size() * sizeof(GLfloat), q1s.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 21, q1Buffer);

		t1s.resize(this->numVertices, glm::vec2(0.0f));
		glGenBuffers(1, &t1Buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, t1Buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, t1s.size() * sizeof(glm::vec2), t1s.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 22, t1Buffer);

		Dt1q1s.resize(this->numVertices, 0.0f);
		glGenBuffers(1, &Dt1q1Buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, Dt1q1Buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, Dt1q1s.size() * sizeof(GLfloat), Dt1q1s.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 23, Dt1q1Buffer);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		//shaders for apparent ridges
		this->viewDepCurvatureCompute = loadComputeShader(".\\shaders\\viewDepCurv.compute");
		this->Dt1q1Compute = loadComputeShader(".\\shaders\\Dt1q1.compute");

		//std::cout << "Ready to render.\n";
		this->isSet = true;

		return;
	}
	//draw function
	bool render(GLuint shader) {

		if (!isSet) { this->setup(); }

		if (this->apparentRidges) {
			//Rebind SSBOs
			this->rebindSSBOs();

			if (!printed) {
				std::cout << "For model " << this->path << " : \n";
				std::cout << "Before View dep computation " << this->path << " : \n";
				glGetNamedBufferSubData(PDBuffer, 0, PDs.size() * sizeof(GLfloat), PDs.data());
				std::cout << "PDs : ";
				for (int dbg = 0; dbg < 2; dbg++) {
					printVec(PDs[dbg]); std::cout << ", ";
					printVec(PDs[dbg+this->numVertices]); std::cout << ", ";
				}
				std::cout << "\n";
				glGetNamedBufferSubData(CurvatureBuffer, 0, PrincipalCurvatures.size() * sizeof(GLfloat), PrincipalCurvatures.data());
				std::cout << "PrincipalCurvatures : ";
				for (int dbg = 0; dbg < 2; dbg++) {
					std::cout << PrincipalCurvatures[dbg] << ", ";
					std::cout << PrincipalCurvatures[dbg+this->numVertices] << ", ";
				}
				std::cout << "\n"; 
				glGetNamedBufferSubData(this->q1Buffer, 0, q1s.size() * sizeof(GLfloat), q1s.data());
				std::cout << "q1s : ";
				for (int dbg = 0; dbg < 4; dbg++) {
					std::cout << q1s[dbg] << ", ";
				}
				std::cout << "\n";
				glGetNamedBufferSubData(t1Buffer, 0, t1s.size() * sizeof(GLfloat), t1s.data());
				std::cout << "t1s : ";
				for (int dbg = 0; dbg < 4; dbg++) {
					std::cout << "(" << t1s[dbg].x << "," << t1s[dbg].y << "), ";
				}
				std::cout << "\n";
				glGetNamedBufferSubData(Dt1q1Buffer, 0, Dt1q1s.size() * sizeof(GLfloat), Dt1q1s.data());
				std::cout << "Dt1q1s : ";
				for (int dbg = 0; dbg < 4; dbg++) {
					std::cout << Dt1q1s[dbg] << ", ";
				}
				std::cout << "\n";
			}

			glBindVertexArray(VAO);
			//Compute View-dep curvatures (q1), and direction (t1)
			glUseProgram(viewDepCurvatureCompute);
			glUniform1ui(glGetUniformLocation(viewDepCurvatureCompute, "verticesSize"), this->numVertices);
			
			glUniformMatrix4fv(glGetUniformLocation(viewDepCurvatureCompute, "model"), 1, GL_FALSE, &this->modelMatrix[0][0]);

			glDispatchCompute(glm::ceil(GLfloat(this->numVertices) / float(workGroupSize)), 1, 1);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			if (!printed) {
				std::cout << "After view dep curvature " << " : \n";
				glGetNamedBufferSubData(this->q1Buffer, 0, q1s.size() * sizeof(GLfloat), q1s.data());
				std::cout << "q1s : ";
				for (int dbg = 0; dbg < 4; dbg++) {
					std::cout << q1s[dbg] << ", ";
				}
				std::cout << "\n";
				glGetNamedBufferSubData(t1Buffer, 0, t1s.size() * sizeof(GLfloat), t1s.data());
				std::cout << "t1s : ";
				for (int dbg = 0; dbg < 4; dbg++) {
					std::cout << "(" << t1s[dbg].x << "," << t1s[dbg].y << "), ";
				}
				std::cout << "\n";
				glGetNamedBufferSubData(Dt1q1Buffer, 0, Dt1q1s.size() * sizeof(GLfloat), Dt1q1s.data());
				std::cout << "Dt1q1s : ";
				for (int dbg = 0; dbg < 4; dbg++) {
					std::cout << Dt1q1s[dbg] << ", ";
				}
				std::cout << "\n";
			}

			//Compute View-dep curvature derivatives (Dt1q1)
			glUseProgram(Dt1q1Compute);
			glUniform1ui(glGetUniformLocation(Dt1q1Compute, "verticesSize"), this->numVertices);
			glUniformMatrix4fv(glGetUniformLocation(Dt1q1Compute, "model"), 1, GL_FALSE, &this->modelMatrix[0][0]);
			glDispatchCompute(glm::ceil(GLfloat(this->numVertices) / float(workGroupSize)), 1, 1);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);



			if (!printed) {
				std::cout << "After Dt1q1 " << " : \n";
				glGetNamedBufferSubData(this->q1Buffer, 0, q1s.size() * sizeof(GLfloat), q1s.data());
				std::cout << "q1s : ";
				for (int dbg = 0; dbg < 4; dbg++) {
					std::cout << q1s[dbg] << ", ";
				}
				std::cout << "\n";
				glGetNamedBufferSubData(t1Buffer, 0, t1s.size() * sizeof(GLfloat), t1s.data());
				std::cout << "t1s : ";
				for (int dbg = 0; dbg < 4; dbg++) {
					std::cout <<"("<< t1s[dbg].x << "," << t1s[dbg].y << "), ";
				}
				std::cout << "\n";
				glGetNamedBufferSubData(Dt1q1Buffer, 0, Dt1q1s.size() * sizeof(GLfloat), Dt1q1s.data());
				std::cout << "Dt1q1s : ";
				for (int dbg = 0; dbg < 4; dbg++) {
					std::cout << Dt1q1s[dbg] << ", ";
				}
				std::cout << "\n";
				printed = true;
			}
			
		}

		glUseProgram(shader);

		
		glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);

		//glDisableVertexAttribArray(0);
		//glDisableVertexAttribArray(1);
		//glBindVertexArray(0);

		return true;
	}
	void boundingBox() {
		//simple implemetation calculating model boundary box size
		float maxX = vertices[0].x, maxY = vertices[0].y, maxZ = vertices[0].z;
		float minX = vertices[0].x, minY = vertices[0].y, minZ = vertices[0].z;
		
		for (int i = 1; i < vertices.size(); i++) {
			(vertices[i].x > maxX) ? maxX = vertices[i].x : 0;
			(vertices[i].y > maxY) ? maxY = vertices[i].y : 0;
			(vertices[i].z > maxZ) ? maxZ = vertices[i].z : 0;
			(vertices[i].x < minX) ? minX = vertices[i].x : 0;
			(vertices[i].y < minY) ? minY = vertices[i].y : 0;
			(vertices[i].z < minZ) ? minZ = vertices[i].z : 0;
		}
		//center of model
		this->center = glm::vec3((maxX + minX) / 2.0f, (maxY + minY) / 2.0f, (maxZ + minZ) / 2.0f);
		this->diagonalLength = glm::length(glm::vec3(maxX - minX, maxY - minY, maxZ - minZ));
		this->modelScaleFactor = 1.0f/diagonalLength;
	}
	float getMinDistance() {
		float minDist = glm::length(vertices[0] - vertices[1]);
		for (int i = 2; i < vertices.size(); i++) {
			float dis = glm::length(vertices[i] - vertices[0]);
			if (dis < minDist && dis>0.0f) minDist = dis;
		}
		return minDist;
	}
	//Calculates principal curvatures and principal directions per vertex
	//Computed per face -> per vertex in two steps
	void computeCurvatures() {
		//generate ssbos to use in compute shader
		//we need to calculate 2 principal directions and 2 principal curvatures per vertex.
		//NOTE : SSBOs do not work well with vec3s. They take them as vec4 anyway or sth. Use vec4.
		auto start = std::chrono::high_resolution_clock::now();

		//So we need to initially compute by face.
		//Load compute shader
		GLuint perFace = loadComputeShader(".\\shaders\\curvature_perFace.compute");
		GLuint perVertex = loadComputeShader(".\\shaders\\curvature_perVertex.compute");

		//resize vertices for curvature values resize(num,value)
		PDs.resize(vertices.size() * 2, glm::vec4(0.0));
		PrincipalCurvatures.resize(vertices.size() * 2, 0.0f);

		// setup SSBOs
		//  &[0] -> .data() 
		//gen -> bind (set to GL state) -> bufferData
		//for writing
		glGenBuffers(1, &PDBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, PDBuffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, PDs.size() * sizeof(glm::vec4), PDs.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, PDBuffer);

		glGenBuffers(1, &CurvatureBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, CurvatureBuffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, PrincipalCurvatures.size() * sizeof(GLfloat), PrincipalCurvatures.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, CurvatureBuffer);

		//for reading
		//Vertex positions
		glGenBuffers(1, &vertexStorageBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, vertexStorageBuffer);
		std::vector<glm::vec4> vertexStorage;
		for (auto v : vertices) { //make vec4s from vec3s
			vertexStorage.push_back(glm::vec4(v, 1.0f));
		}
		glBufferData(GL_SHADER_STORAGE_BUFFER, vertexStorage.size() * sizeof(glm::vec4), vertexStorage.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, vertexStorageBuffer);

		//normals
		glGenBuffers(1, &normalStorageBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, normalStorageBuffer);
		std::vector<glm::vec4> normalStorage;
		for (auto v : normals) {
			normalStorage.push_back(glm::vec4(v, 0.0f));
		}
		glBufferData(GL_SHADER_STORAGE_BUFFER, normalStorage.size() * sizeof(glm::vec4), normalStorage.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, normalStorageBuffer);

		glGenBuffers(1, &indexStorageBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, indexStorageBuffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, indexStorageBuffer);

		//Curvature tensor elements for mid use
		std::vector<GLfloat> curv1s, curv2s, curv12s;
		curv1s.resize(vertices.size(),0.0f); curv2s.resize(vertices.size(), 0.0f); curv12s.resize(vertices.size(), 0.0f);

		GLuint curv1Buffer, curv2Buffer, curv12Buffer;
		glGenBuffers(1, &curv1Buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, curv1Buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, curv1s.size() * sizeof(GLfloat), curv1s.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, curv1Buffer);

		glGenBuffers(1, &curv2Buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, curv2Buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, curv2s.size() * sizeof(GLfloat), curv2s.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, curv2Buffer); glGenBuffers(1, &curv2Buffer);

		glGenBuffers(1, &curv12Buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, curv12Buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, curv12s.size() * sizeof(GLfloat), curv12s.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14, curv12Buffer);

		//Compute point areas
		this->computePointAreas();

		//Use first compute shader for curvature computation
		glUseProgram(perFace);

		//hmm.. we move by indices so send indicies size.
		glUniform1ui(glGetUniformLocation(perFace, "indicesSize"), this->numIndices);
		glUniform1ui(glGetUniformLocation(perFace, "verticesSize"), this->numVertices);

		//
		//Dispatch -> run compute shader in GPU 
		//As we have workGroupSize invocations per work group, to run per face :
		glDispatchCompute(glm::ceil((GLfloat(this->numIndices) / 3.0f) / float(workGroupSize)), 1, 1);

		//Barrier to ensure coherency
		//glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);


		/*
		glGetNamedBufferSubData(PDBuffer, 0, PDs.size() * sizeof(glm::vec4), PDs.data());
		for (int dbg = 0; dbg < 2; dbg++) {
			std::cout << "PDs[" << dbg << "] "; printVec(PDs[dbg]); std::cout << "\n";
			std::cout << "PDs[" << vertices.size() + dbg << "] "; printVec(PDs[vertices.size()]); std::cout << "\n";
			std::cout << "PDs[" << PDs.size() - dbg - 1 << "] "; printVec(PDs[PDs.size() - dbg - 1]); std::cout << "\n";
		}
		*/
		//Now run per vertex
		glUseProgram(perVertex);

		glUniform1ui(glGetUniformLocation(perVertex, "indicesSize"), this->numIndices);
		glUniform1ui(glGetUniformLocation(perVertex, "verticesSize"), this->numVertices);

		//Dispatch -> run compute shader in GPU 
		//As we have workGroupSize invocations per work group, to run per vertex :
		glDispatchCompute(glm::ceil(GLfloat(this->numVertices) / float(workGroupSize)), 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		/*
		*/
		glGetNamedBufferSubData(PDBuffer, 0, PDs.size() * sizeof(glm::vec4), PDs.data());
		/*
		std::cout << "PDs after per vertex compute : " << "\n";
		for (int dbg = 0; dbg < 2; dbg++) {
			std::cout << "PDs[" << dbg << "] "; printVec(PDs[dbg]); std::cout << "\n";
			std::cout << "PDs[" << vertices.size() + dbg << "] "; printVec(PDs[vertices.size()]); std::cout << "\n";
			std::cout << "PDs[" << PDs.size() - dbg - 1 << "] "; printVec(PDs[PDs.size() - dbg - 1]); std::cout << "\n";
		}
		*/

		//is CUDA necessary?

		this->curvaturesCalculated = true;

		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed_seconds = end - start;
		std::cout << "Curvatures for " << this->path << " calculated on compute shader. Took "<<elapsed_seconds.count()<<" seconds.\n";

		//glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0); //unbind
		/*
		glDeleteBuffers(1, &vertexStorageBuffer);
		glDeleteBuffers(1, &normalStorageBuffer);
		glDeleteBuffers(1, &indexStorageBuffer);
		
		
		*/
	}
	
	void computeHessian() {

	}
	//Finds adjacent vertices for each vertex
	void findAdjacentFaces() {
		auto start = std::chrono::high_resolution_clock::now();
		this->adjacentFaces.resize(vertices.size()); //adjacentFaces<std::array<int, 20>
		for (int i = 0; i < numVertices;i++) { adjacentFaces[i].fill(-1); }
		/*
		//Computing this on the cpu is rather slow so I made a shader for it.
		*/
		glGenBuffers(1, &adjacentFacesBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, adjacentFacesBuffer);  
		glBufferData(GL_SHADER_STORAGE_BUFFER, adjacentFaces.size() * sizeof(int) * 20, adjacentFaces.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 20, adjacentFacesBuffer);

		GLuint adjacentFacesCompute = loadComputeShader(".\\shaders\\adjacentFaces.compute");
		glUseProgram(adjacentFacesCompute);
		glUniform1ui(glGetUniformLocation(adjacentFacesCompute, "indicesSize"), this->numIndices);
		glDispatchCompute(glm::ceil((GLfloat(this->numIndices) / 3.0f) / float(workGroupSize)), 1, 1); //per face
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		
		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed_seconds = end - start;
		std::cout << "Adjacent faces calculated. Took : "<< elapsed_seconds.count() <<" seconds. \n";

		glGetNamedBufferSubData(adjacentFacesBuffer, 0, adjacentFaces.size() * sizeof(GLfloat), adjacentFaces.data());
		std::cout << "First vertex's adjacent vertices : ";
		for (int j = 0; j < 10; j++) {
			std::cout << this->adjacentFaces[0][2 * j] << ", " << this->adjacentFaces[0][2 * j + 1] << ". ";
		}
		std::cout << "\n";
	}
	//Calculates pseudo-"Voronoi" area for each vertex
	void computePointAreas() {
		pointAreaCompute = loadComputeShader(".\\shaders\\pointAreas.compute");
		glUseProgram(pointAreaCompute);

		pointAreas.resize(this->numVertices,0.0f); //by vertex
		glGenBuffers(1, &pointAreaBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, pointAreaBuffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, pointAreas.size() * sizeof(GLfloat), pointAreas.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 30, pointAreaBuffer);

		cornerAreas.resize(this->numIndices, 0.0f); //by index (for faces)
		glGenBuffers(1, &cornerAreaBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, cornerAreaBuffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, cornerAreas.size() * sizeof(GLfloat), cornerAreas.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 31, cornerAreaBuffer);

		glUniform1ui(glGetUniformLocation(pointAreaCompute, "indicesSize"), this->numIndices);
		glUniform1ui(glGetUniformLocation(pointAreaCompute, "verticesSize"), this->numVertices);

		glDispatchCompute(glm::ceil((GLfloat(this->numIndices) / 3.0f) / float(workGroupSize)), 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		/*
		glGetNamedBufferSubData(cornerAreaBuffer, 0, cornerAreas.size() * sizeof(GLfloat), cornerAreas.data());
		glGetNamedBufferSubData(pointAreaBuffer, 0, pointAreas.size() * sizeof(GLfloat), pointAreas.data());
		std::cout << "Corner Areas after compute : " << "\n";
		for (int dbg = 0; dbg < 2; dbg++) {
			std::cout << "cornerAreas[" << dbg<< "] " << cornerAreas[dbg] << " ,";
			std::cout << "cornerAreas[" << cornerAreas.size() - dbg - 1 << "] "<< cornerAreas[cornerAreas.size() - dbg - 1]<<" "; std::cout << "\n";
		}std::cout << "Point Areas after compute : " << "\n";
		for (int dbg = 0; dbg < 2; dbg++) {
			std::cout << "pointAreas[" << dbg << "] " << pointAreas[dbg] << " ,";
			std::cout << "pointAreas[" << pointAreas.size() - dbg - 1 << "] " << pointAreas[pointAreas.size() - dbg - 1] << " "; std::cout << "\n";
		}
		*/
	
	}
	bool loadAssimp() {
		Assimp::Importer importer;
		
		if (!this->vertices.empty()) {
			return false; //if not empty return
		}
		
		std::cout<<"Loading file : "<<this->path<<".\n";
		//aiProcess_Triangulate !!!
		const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace | aiProcess_GenUVCoords); //aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);
		if (!scene) {
			fprintf(stderr, importer.GetErrorString());
			return false;
		}
		std::cout << "Number of meshes : " << scene->mNumMeshes << ".\n";

		// TODO : In this code we just use the 1st mesh (for now)
		const aiMesh* mesh = scene->mMeshes[0];
		// Fill vertices positions
		//std::cout << "Number of vertices :" << mesh->mNumVertices << "\n";
		for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
			aiVector3D pos = mesh->mVertices[i];
			this->vertices.push_back(glm::vec3(pos.x, pos.y, pos.z));
		}

		// Fill vertices texture coordinates
		if (mesh->HasTextureCoords(0)) {
			for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
				aiVector3D UVW = mesh->mTextureCoords[0][i]; // Assume only 1 set of UV coords; AssImp supports 8 UV sets.
				this->textureCoordinates.push_back(glm::vec2(UVW.x, UVW.y));
			}
		}

		// Fill vertices normals
		if (mesh->HasNormals()) {
			for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
				// std::cout<<"Number of Vertices : "<<mesh->mNumVertices<<"\n";
				aiVector3D n = mesh->mNormals[i];
				this->normals.push_back(glm::normalize(glm::vec3(n.x, n.y, n.z)));
			}
		}
		else {
			//std::cout << "Model has no normals.\n";
			//mesh->
			for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
				// std::cout<<"Number of Vertices : "<<mesh->mNumVertices<<"\n";
				aiVector3D n = mesh->mNormals[i];
				this->normals.push_back(glm::normalize(glm::vec3(n.x, n.y, n.z)));
			}
		}
		// Fill face indices
		for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
			std::array<unsigned int, 3> fac;
			for (unsigned int j = 0; j < mesh->mFaces[i].mNumIndices; j++) {
				this->indices.push_back(mesh->mFaces[i].mIndices[j]);
				fac[j] = mesh->mFaces[i].mIndices[j];
			}
			this->faces.push_back(fac);
		}

		std::cout << "Number of vertices : " << this->vertices.size() << "\n";
		std::cout << "Number of normals : " << this->normals.size() << "\n";
		std::cout << "Number of indices : " << this->indices.size() << "\n";
		std::cout << "Number of faces : " << this->faces.size() << "\n";
		
		this->numVertices = this->vertices.size();
		this->numNormals = this->normals.size();
		this->numFaces = this->faces.size();
		this->numIndices = this->indices.size();

		// The "scene" pointer will be deleted automatically by "importer"
		return true;
	}
	bool exportAssimp() {

	}
	bool rebindSSBOs() {

		//Rebind SSBOs
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, PDBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, CurvatureBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, vertexStorageBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, normalStorageBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, indexStorageBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 20, adjacentFacesBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 21, q1Buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 22, t1Buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 23, Dt1q1Buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 30, pointAreaBuffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 31, cornerAreaBuffer);
		return true;
	}

	//destructor
	//I should also be using smart pointers for the meshes / models
	//Smart pointers should NOT be in another smart pointer.
	//Nested ownership often causes leaks
	/*
	~Model() {
		
		glDeleteBuffers(1, &positionBuffer);
		glDeleteBuffers(1, &normalBuffer);
		glDeleteBuffers(1, &textureBuffer);
		glDeleteBuffers(1, &EBO);
		glDeleteBuffers(1, &PDBuffer);
		glDeleteBuffers(1, &CurvatureBuffer);
		glDeleteBuffers(1, &vertexStorageBuffer);
		glDeleteBuffers(1, &normalStorageBuffer);
		glDeleteBuffers(1, &indexStorageBuffer);
		glDeleteBuffers(1, &adjacentFacesBuffer);
		glDeleteBuffers(1, &q1Buffer);
		glDeleteBuffers(1, &t1Buffer);
		glDeleteBuffers(1, &Dt1q1Buffer);
		glDeleteBuffers(1, &pointAreaBuffer);
		glDeleteBuffers(1, &cornerAreaBuffer);
	}
	*/
};
//end of Model class


bool loadAssimp(
	const char* path,
	std::vector<glm::vec3>& out_vertices,
	std::vector<glm::vec3>& out_normals,
	std::vector<unsigned int>& out_indices
) {
	std::vector<unsigned int> indices;
	std::vector<glm::vec3> vertices;
	std::vector<glm::vec2> uvs;
	std::vector<glm::vec3> normals;
	Assimp::Importer importer;
	printf("Loading file : %s...\n", path);
	//aiProcess_Triangulate !!!
	const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals| aiProcess_JoinIdenticalVertices| aiProcess_CalcTangentSpace |aiProcess_GenUVCoords ); //aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);
	if (!scene) {
		fprintf(stderr, importer.GetErrorString());
		return false;
	}
	// TODO : In this code we just use the 1st mesh (for now)
	const aiMesh* mesh = scene->mMeshes[0]; 
	// Fill vertices positions
	//std::cout << "Number of vertices :" << mesh->mNumVertices << "\n";
	for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
		aiVector3D pos = mesh->mVertices[i];
		out_vertices.push_back(glm::vec3(pos.x, pos.y, pos.z));
	}

	// Fill vertices texture coordinates
	if (mesh->HasTextureCoords(0)) {
		for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
			aiVector3D UVW = mesh->mTextureCoords[0][i]; // Assume only 1 set of UV coords; AssImp supports 8 UV sets.
			uvs.push_back(glm::vec2(UVW.x, UVW.y));
		}
	}

	// Fill vertices normals
	if (mesh->HasNormals()) {
		for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
			// std::cout<<"Number of Vertices : "<<mesh->mNumVertices<<"\n";
			aiVector3D n = mesh->mNormals[i];
			out_normals.push_back(glm::normalize(glm::vec3(n.x, n.y, n.z)));
		}
	}  
	else {
		//std::cout << "Model has no normals.\n";
		//mesh->
		for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
			// std::cout<<"Number of Vertices : "<<mesh->mNumVertices<<"\n";
			aiVector3D n = mesh->mNormals[i];
			out_normals.push_back(glm::normalize(glm::vec3(n.x, n.y, n.z)));
		}
	}
	// Fill face indices
	//std::cout << "Number of faces :" << mesh->mNumFaces << "\n";
	//std::cout << "Number of indices in faces :" << mesh->mFaces[0].mNumIndices << "\n";
	for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
		for (unsigned int j = 0; j < mesh->mFaces[i].mNumIndices; j++)
			out_indices.push_back(mesh->mFaces[i].mIndices[j]);
	}

	std::cout << "Size of vertices : " << out_vertices.size() << "\n";
	std::cout << "Size of normals : " << out_normals.size() << "\n";
	std::cout << "Size of indices : " << out_indices.size() << "\n";


	// The "scene" pointer will be deleted automatically by "importer"
	return true;
}
