/*******************************************************************************
*                                                                              *
*   PrimeSense NiTE 2.0 - User Viewer Sample                                   *
*   Copyright (C) 2012 PrimeSense Ltd.                                         *
*                                                                              *
*******************************************************************************/

#include <GL/glut.h>
#include <stdio.h>			// Header File For Standard Input/Output
#include <gl/glu.h>			// Header File For The GLu32 Library
#include <windows.h>
#include <fstream>
#include <string.h>

#include "lodepng.h"

#include "Viewer.h"

#define GL_WIN_SIZE_X	1280
#define GL_WIN_SIZE_Y	1024
#define TEXTURE_SIZE	512

#define MAX_USERS 1

#define MAX_LINE_LENGTH 1000

#define DEFAULT_DISPLAY_MODE	DISPLAY_MODE_DEPTH

#define MIN_NUM_CHUNKS(data_size, chunk_size)	((((data_size)-1) / (chunk_size) + 1))
#define MIN_CHUNKS_SIZE(data_size, chunk_size)	(MIN_NUM_CHUNKS(data_size, chunk_size) * (chunk_size))

SampleViewer* SampleViewer::ms_self = NULL;

bool g_drawSkeleton = true;
bool g_drawCenterOfMass = false;
bool g_drawStatusLabel = true;
bool g_drawBoundingBox = false;
bool g_drawBackground = false;
bool g_drawDepth = true;
bool g_drawFrameId = false;
bool g_fullScreen = true;

/*
	String zur Ausgabe der Position der Betrachter
*/
char msgBuffer[100];

/*
	Speichert die Positionen der Betrachter im Raum
*/
int personCenter;
int personDistance;
float xOnScreen,yOnScreen;

/*
	Speichert den jeweils letzten Wert von xValue, wenn der zwischen 0 und Fensterbreite liegt.
*/
int imageBorder;

/*
	Speichert den Index des neuesten Betrachters.
	Wird ein neuer Betrachter erkannt, verliert der aktuelle den Fokus und der neue Betrachter wird hier gespeichert.
	Die Bildgrenze folgt dann dem neuen Betrachter.

	Wenn der aktuelle Betrachter verloren wurde, läuft die Bildgrenze automatisch ab.
	Dann wird -1 gespeichert.
*/
nite::UserId latestUserID=-1;

nite::UserData latestUser;

/*
	Speichert, ob ein Besucher getrackt wird oder nicht
*/
bool trackLatestUser=false;

/*
	Speichert die X-Positionen aller User
*/
typedef struct UuserPosition{
	unsigned int xPos;
	nite::UserId id;
} UserPosition;
std::vector<UserPosition> userPositions;

/*
	Speichert die Position der Kinect
	false => Kinect steht vor dem Betrachter
	true => Kinect steht hinter dem Betrachter
*/
bool mirrored=false;

nite::UserTrackerFrameRef userTrackerFrame;


/*
	Speichert die dargestellten Bilder
*/
typedef struct pngImage{
	std::vector<unsigned char> img;
	int width,height;
} PNGImage;

typedef struct imagePair{
	PNGImage original, overlay;
} ImagePair;
std::vector<ImagePair> imagePairs;

/*
	Speichert den Index des aktuellen Bildpaares
*/
int imagePairID;

int g_nXRes = 0, g_nYRes = 0;

// time to hold in pose to exit program. In milliseconds.
const int g_poseTimeoutToExit = 100;

int loadPNGImage(char* filename,PNGImage& target)
{
	printf("Lade Bild %s\n",filename);

	PNGImage loadedImage;

	std::vector<unsigned char> image;
	unsigned int width, height;
	unsigned int error = lodepng::decode(image, width, height, filename);

	if(error!=0)	//Fehler beim Laden
	{
		printf("Fehler beim Laden von %s, Error: %d\n",filename,error);
		return error;
	}
	// Texture size must be power of two for the primitive OpenGL version this is written for. Find next power of two.
	size_t u2 = 1; 
	while(u2 < width) 
		u2 *= 2;
	size_t v2 = 1; 
	while(v2 < height) 
		v2 *= 2;
	// Ratio for power of two version compared to actual version, to render the non power of two image with proper size.
	double u3 = (double)width / u2;
	double v3 = (double)height / v2;

	// Make power of two version of the image.
	std::vector<unsigned char> image2(u2 * v2 * 4);
	for(size_t y = 0; y < height; y++)
		for(size_t x = 0; x < width; x++)
			for(size_t c = 0; c < 4; c++)
			{
				image2[4 * u2 * y + 4 * x + c] = image[4 * width * y + 4 * x + c];
			}

	target.img=image2;
	target.width=u2;
	target.height=v2;

	printf("Bild wurde geladen\n\tBreite: %d\tHoehe: %d\n",target.width,target.height);

	return 0;

}

int loadImagePair(char* filename1,char* filename2)
{
	ImagePair pair;
	if(	loadPNGImage(filename1,pair.original)==0 &&
		loadPNGImage(filename2,pair.overlay)==0) 
	{
		imagePairs.push_back(pair);
		return 0;
	}
	return 1;
}

void SampleViewer::glutIdle()
{
	glutPostRedisplay();
}

void SampleViewer::glutDisplay()
{
	SampleViewer::ms_self->Display();
}

void SampleViewer::glutKeyboard(unsigned char key, int x, int y)
{
	SampleViewer::ms_self->OnKey(key, x, y);
}

SampleViewer::SampleViewer(const char* strSampleName) : m_poseUser(0)
{
	ms_self = this;
	strncpy(m_strSampleName, strSampleName, ONI_MAX_STR);
	m_pUserTracker = new nite::UserTracker;
}

SampleViewer::~SampleViewer()
{
	Finalize();

	delete[] m_pTexMap;

	ms_self = NULL;
}

void SampleViewer::Finalize()
{
	delete m_pUserTracker;
	nite::NiTE::shutdown();
	openni::OpenNI::shutdown();
}

openni::Status SampleViewer::Init(int argc, char **argv)
{
	m_pTexMap = NULL;

	openni::Status rc = openni::OpenNI::initialize();
	if (rc != openni::STATUS_OK)
	{
		printf("Failed to initialize OpenNI\n%s\n", openni::OpenNI::getExtendedError());
		return rc;
	}

	const char* deviceUri = openni::ANY_DEVICE;
	for (int i = 1; i < argc-1; ++i)
	{
		if (strcmp(argv[i], "-device") == 0)
		{
			deviceUri = argv[i+1];
			break;
		}
	}

	rc = m_device.open(deviceUri);
	if (rc != openni::STATUS_OK)
	{
		printf("Failed to open device\n%s\n", openni::OpenNI::getExtendedError());
		return rc;
	}

	nite::NiTE::initialize();

	if (m_pUserTracker->create(&m_device) != nite::STATUS_OK)
	{
		return openni::STATUS_ERROR;
	}

	/*
		mirrored?
	*/
	if(argc==2){
		if(strcmp(argv[1],"mirror")==0){
			mirrored=true;
		}
	}

	/*
		Einstellungsdatei laden (Parameter 1)
		Bilddateien laden
	*/
	std::vector<std::string> config;

	std::ifstream ifs(argv[1]);
	std::string temp;

	printf("Loading Config: %s\n",argv[1]);
	while(std::getline(ifs,temp))
	{
		config.push_back(temp);
	}

	char *filename1=(char*)malloc(MAX_LINE_LENGTH),*filename2=(char*)malloc(MAX_LINE_LENGTH),*temp2=(char*)malloc(MAX_LINE_LENGTH);
	for(int i=0;i<config.size();i++)
	{

		printf("\tAnalyzing line %d: '%s'\n",i+1,config.at(i));
		/*
			Config-Zeilen analysieren
		*/
		int k;
		for(k=0;k<config.at(i).size();k++)
		{
			temp2[k]=(char)config.at(i)[k];
		}
		temp2[k]='\0';
		// analysiere Bildpaare
		sscanf(temp2,"pair %s %s",filename1,filename2);

		if(loadImagePair(filename1,filename2)==0)
		{
			printf("Image Pair loaded:\n\t%s\n\t%s\n\t=>%d pairs\n\n",filename1,filename2,imagePairs.size());
		}

	}

	imagePairID=0;

	return InitOpenGL(argc, argv);

}

openni::Status SampleViewer::Run()	//Does not return
{
	glutMainLoop();

	return openni::STATUS_OK;
}

int colorCount = MAX_USERS;
float Colors[][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}};

bool g_visibleUsers[MAX_USERS] = {false};
nite::SkeletonState g_skeletonStates[MAX_USERS] = {nite::SKELETON_NONE};
char g_userStatusLabels[MAX_USERS][100] = {{0}};

char g_generalMessage[100] = {0};

#define USER_MESSAGE(msg) {\
	sprintf(g_userStatusLabels[user.getId()],"%s", msg);\
	printf("[%08llu] User #%d:\t%s\n",ts, user.getId(),msg);}


void updateUserState(const nite::UserData& user, uint64_t ts)
{

	if (user.isNew())
	{
		USER_MESSAGE("New");
	}
	else if (user.isVisible() && !g_visibleUsers[user.getId()])
		printf("[%08llu] User #%d:\tVisible\n", ts, user.getId());
	else if (!user.isVisible() && g_visibleUsers[user.getId()])
		printf("[%08llu] User #%d:\tOut of Scene\n", ts, user.getId());
	else if (user.isLost())
	{
		USER_MESSAGE("Lost");
	}
	g_visibleUsers[user.getId()] = user.isVisible();


	if(g_skeletonStates[user.getId()] != user.getSkeleton().getState())
	{
		switch(g_skeletonStates[user.getId()] = user.getSkeleton().getState())
		{
		case nite::SKELETON_NONE:
			{
//				USER_MESSAGE("Stopped tracking.");
				latestUserID=-1;
			}
			break;
		case nite::SKELETON_TRACKED:
			{
//				USER_MESSAGE("Tracking!");
				if(latestUserID==-1)
					latestUserID=user.getId();
			}
			break;
		case nite::SKELETON_CALIBRATION_ERROR_NOT_IN_POSE:
		case nite::SKELETON_CALIBRATION_ERROR_HANDS:
		case nite::SKELETON_CALIBRATION_ERROR_LEGS:
		case nite::SKELETON_CALIBRATION_ERROR_HEAD:
		case nite::SKELETON_CALIBRATION_ERROR_TORSO:
			USER_MESSAGE("Calibration Failed... :-|")
			break;
		case nite::SKELETON_CALIBRATING:
			break;
		}
	}
}

#ifndef USE_GLES
void glPrintString(void *font, const char *str)
{
	int i,l = (int)strlen(str);

	for(i=0; i<l; i++)
	{   
		glutBitmapCharacter(font,*str++);
	}   
}
#endif

void printUserInfo(int y,int color,char* msg)
{
	glColor3f(1,1,1);
	glRasterPos2i(40,y);
	glPrintString(GLUT_BITMAP_TIMES_ROMAN_24, msg);
}


void DrawStatusLabel(nite::UserTracker* pUserTracker, const nite::UserData& user)
{
	int color = user.getId() % colorCount;
	glColor3f(1.0f - Colors[color][0], 1.0f - Colors[color][1], 1.0f - Colors[color][2]);

	float x,y;
	pUserTracker->convertJointCoordinatesToDepth(user.getCenterOfMass().x, user.getCenterOfMass().y, user.getCenterOfMass().z, &x, &y);
	x *= GL_WIN_SIZE_X/g_nXRes;
	y *= GL_WIN_SIZE_Y/g_nYRes;
	char *msg = g_userStatusLabels[user.getId()];
	glRasterPos2i(x-((strlen(msg)/2)*8),y);
	glPrintString(GLUT_BITMAP_HELVETICA_18, msg);
}

void DrawFrameId(int frameId)
{
	char buffer[80] = "";
	sprintf(buffer, "%d", frameId);
	glColor3f(1.0f, 0.0f, 0.0f);
	glRasterPos2i(20, 20);
	glPrintString(GLUT_BITMAP_HELVETICA_18, buffer);
}

void DrawCenterOfMass(nite::UserTracker* pUserTracker, const nite::UserData& user)
{
	glColor3f(1.0f, 1.0f, 1.0f);

	float coordinates[3] = {0};

	pUserTracker->convertJointCoordinatesToDepth(user.getCenterOfMass().x, user.getCenterOfMass().y, user.getCenterOfMass().z, &coordinates[0], &coordinates[1]);

	coordinates[0] *= GL_WIN_SIZE_X/g_nXRes;
	coordinates[1] *= GL_WIN_SIZE_Y/g_nYRes;
	glPointSize(8);
	glVertexPointer(3, GL_FLOAT, 0, coordinates);
	glDrawArrays(GL_POINTS, 0, 1);

}

void DrawBoundingBox(const nite::UserData& user)
{
	glColor3f(1.0f, 1.0f, 1.0f);

	float coordinates[] =
	{
		user.getBoundingBox().max.x, user.getBoundingBox().max.y, 0,
		user.getBoundingBox().max.x, user.getBoundingBox().min.y, 0,
		user.getBoundingBox().min.x, user.getBoundingBox().min.y, 0,
		user.getBoundingBox().min.x, user.getBoundingBox().max.y, 0,
	};
	coordinates[0]  *= GL_WIN_SIZE_X/g_nXRes;
	coordinates[1]  *= GL_WIN_SIZE_Y/g_nYRes;
	coordinates[3]  *= GL_WIN_SIZE_X/g_nXRes;
	coordinates[4]  *= GL_WIN_SIZE_Y/g_nYRes;
	coordinates[6]  *= GL_WIN_SIZE_X/g_nXRes;
	coordinates[7]  *= GL_WIN_SIZE_Y/g_nYRes;
	coordinates[9]  *= GL_WIN_SIZE_X/g_nXRes;
	coordinates[10] *= GL_WIN_SIZE_Y/g_nYRes;

	glPointSize(2);
	glVertexPointer(3, GL_FLOAT, 0, coordinates);
	glDrawArrays(GL_LINE_LOOP, 0, 4);

}

void DrawLimb(nite::UserTracker* pUserTracker, const nite::SkeletonJoint& joint1, const nite::SkeletonJoint& joint2, int color)
{
	float coordinates[6] = {0};
	pUserTracker->convertJointCoordinatesToDepth(joint1.getPosition().x, joint1.getPosition().y, joint1.getPosition().z, &coordinates[0], &coordinates[1]);
	pUserTracker->convertJointCoordinatesToDepth(joint2.getPosition().x, joint2.getPosition().y, joint2.getPosition().z, &coordinates[3], &coordinates[4]);

	coordinates[0] *= GL_WIN_SIZE_X/g_nXRes;
	coordinates[1] *= GL_WIN_SIZE_Y/g_nYRes;
	coordinates[3] *= GL_WIN_SIZE_X/g_nXRes;
	coordinates[4] *= GL_WIN_SIZE_Y/g_nYRes;

	if (joint1.getPositionConfidence() == 1 && joint2.getPositionConfidence() == 1)
	{
		glColor3f(1.0f - Colors[color][0], 1.0f - Colors[color][1], 1.0f - Colors[color][2]);
	}
	else if (joint1.getPositionConfidence() < 0.5f || joint2.getPositionConfidence() < 0.5f)
	{
		return;
	}
	else
	{
		glColor3f(.5, .5, .5);
	}
	glPointSize(2);
	glVertexPointer(3, GL_FLOAT, 0, coordinates);
	glDrawArrays(GL_LINES, 0, 2);

	glPointSize(10);
	if (joint1.getPositionConfidence() == 1)
	{
		glColor3f(1.0f - Colors[color][0], 1.0f - Colors[color][1], 1.0f - Colors[color][2]);
	}
	else
	{
		glColor3f(.5, .5, .5);
	}
	glVertexPointer(3, GL_FLOAT, 0, coordinates);
	glDrawArrays(GL_POINTS, 0, 1);

	if (joint2.getPositionConfidence() == 1)
	{
		glColor3f(1.0f - Colors[color][0], 1.0f - Colors[color][1], 1.0f - Colors[color][2]);
	}
	else
	{
		glColor3f(.5, .5, .5);
	}
	glVertexPointer(3, GL_FLOAT, 0, coordinates+3);
	glDrawArrays(GL_POINTS, 0, 1);
}

void DrawSkeleton(nite::UserTracker* pUserTracker, const nite::UserData& userData, int y)
{
/*	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_HEAD), userData.getSkeleton().getJoint(nite::JOINT_NECK), userData.getId() % colorCount);

	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_LEFT_SHOULDER), userData.getSkeleton().getJoint(nite::JOINT_LEFT_ELBOW), userData.getId() % colorCount);
	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_LEFT_ELBOW), userData.getSkeleton().getJoint(nite::JOINT_LEFT_HAND), userData.getId() % colorCount);

	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_RIGHT_SHOULDER), userData.getSkeleton().getJoint(nite::JOINT_RIGHT_ELBOW), userData.getId() % colorCount);
	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_RIGHT_ELBOW), userData.getSkeleton().getJoint(nite::JOINT_RIGHT_HAND), userData.getId() % colorCount);

	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_LEFT_SHOULDER), userData.getSkeleton().getJoint(nite::JOINT_RIGHT_SHOULDER), userData.getId() % colorCount);

	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_LEFT_SHOULDER), userData.getSkeleton().getJoint(nite::JOINT_TORSO), userData.getId() % colorCount);
	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_RIGHT_SHOULDER), userData.getSkeleton().getJoint(nite::JOINT_TORSO), userData.getId() % colorCount);

	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_TORSO), userData.getSkeleton().getJoint(nite::JOINT_LEFT_HIP), userData.getId() % colorCount);
	DrawLimb(pUserTracker, userData.getSkeleton().getJoint(nite::JOINT_TORSO), userData.getSkeleton().getJoint(nite::JOINT_RIGHT_HIP), userData.getId() % colorCount);
*/
	////////
	const nite::SkeletonJoint joint = userData.getSkeleton().getJoint(nite::JOINT_HEAD);

	pUserTracker->convertJointCoordinatesToDepth(
		joint.getPosition().x,
		joint.getPosition().y,
		joint.getPosition().z,
		&xOnScreen, 
		&yOnScreen);

	imageBorder = xOnScreen * (GL_WIN_SIZE_X / g_nXRes);
	if(imageBorder < 0)
		imageBorder = 0;
	if(imageBorder > GL_WIN_SIZE_X)
		imageBorder = GL_WIN_SIZE_X;

	sprintf(msgBuffer,"Tracking User %d | xPos: %d",userData.getId(),imageBorder);
	printUserInfo(y, 0, msgBuffer);

}


/*
	PRE: left <= value <= right
*/
int getNearestIndex(int left,int right,int value){
	if(right-value>value-left)
		return 1;
	return 0;
}

void TrackUser(nite::UserTracker* pUserTracker, const nite::UserData& user)
{
	const nite::SkeletonJoint joint = user.getSkeleton().getJoint(nite::JOINT_HEAD);

	pUserTracker->convertJointCoordinatesToDepth(
		joint.getPosition().x,
		joint.getPosition().y,
		joint.getPosition().z,
		&xOnScreen, 
		&yOnScreen);

	imageBorder = xOnScreen * (GL_WIN_SIZE_X / g_nXRes);
	if(imageBorder < 0)
		imageBorder = 0;
	if(imageBorder > GL_WIN_SIZE_X)
		imageBorder = GL_WIN_SIZE_X;
}

void TrackLatestUser(nite::UserTracker* pUserTracker)
{
		
//	const nite::UserData user = userTrackerFrame.getUserById(latestUserID);

	pUserTracker->convertJointCoordinatesToDepth(
		latestUser.getCenterOfMass().x, 
		latestUser.getCenterOfMass().y, 
		latestUser.getCenterOfMass().z, 
		&xOnScreen, 
		&yOnScreen);

	int xValue = xOnScreen * (GL_WIN_SIZE_X / g_nXRes);

	if(xValue>0 && xValue<=GL_WIN_SIZE_X)
	{
		imageBorder=xValue;
	}

}

void drawImage(PNGImage img)
{
	/*
		Bilder zeichnen
	*/
	glEnable(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); //GL_NEAREST = no smoothing
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(
		GL_TEXTURE_2D,			//targte
		0,						//level
		4,						//internal format
		img.width,						//breite 
		img.height,						//hoehe
		0,						//border
		GL_RGBA,					//format
		GL_UNSIGNED_BYTE,		//typ
		&img.img[0]				//zeiger auf pixel
	);

	glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
      glTexCoord2f( 0.0f, 0.0f); 
	  glVertex2f( 0, 0);
      
	  glTexCoord2f( 1.0f, 0.0f); 
	  glVertex2f( img.width, 0);
      
	  glTexCoord2f( 1.0f, 1.0f); 
	  glVertex2f( img.width, img.height);
      
	  glTexCoord2f( 0.0f, 1.0f); 
	  glVertex2f( 0, img.height);
    glEnd();

	glDisable(GL_TEXTURE_2D);
}

////////////////////////////


void SampleViewer::Display()
{
	openni::VideoFrameRef depthFrame;
	nite::Status rc = m_pUserTracker->readFrame(&userTrackerFrame);
	if (rc != nite::STATUS_OK)
	{
		printf("GetNextData failed\n");
		return;
	}

	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION_MATRIX);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, GL_WIN_SIZE_X, GL_WIN_SIZE_Y, 0, -1.0, 1.0);

	/*
		Originalbild zeichnen
	*/
	drawImage(imagePairs.at(imagePairID).original);

	depthFrame = userTrackerFrame.getDepthFrame();
	g_nXRes = depthFrame.getVideoMode().getResolutionX();
	g_nYRes = depthFrame.getVideoMode().getResolutionY();
	
	/*
		glColor4f(1,1,1,1);

	depthFrame = userTrackerFrame.getDepthFrame();

	if (m_pTexMap == NULL)
	{
		// Texture map init
		m_nTexMapX = MIN_CHUNKS_SIZE(depthFrame.getVideoMode().getResolutionX(), TEXTURE_SIZE);
		m_nTexMapY = MIN_CHUNKS_SIZE(depthFrame.getVideoMode().getResolutionY(), TEXTURE_SIZE);
		m_pTexMap = new openni::RGB888Pixel[m_nTexMapX * m_nTexMapY];
	}

	const nite::UserMap& userLabels = userTrackerFrame.getUserMap();

	if (depthFrame.isValid() && g_drawDepth)
	{
		const openni::DepthPixel* pDepth = (const openni::DepthPixel*)depthFrame.getData();
		int width = depthFrame.getWidth();
		int height = depthFrame.getHeight();
		// Calculate the accumulative histogram (the yellow display...)
		memset(m_pDepthHist, 0, MAX_DEPTH*sizeof(float));
		int restOfRow = depthFrame.getStrideInBytes() / sizeof(openni::DepthPixel) - width;

		unsigned int nNumberOfPoints = 0;
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x, ++pDepth)
			{
				if (*pDepth != 0)
				{
					m_pDepthHist[*pDepth]++;
					nNumberOfPoints++;
				}
			}
			pDepth += restOfRow;
		}
		for (int nIndex=1; nIndex<MAX_DEPTH; nIndex++)
		{
			m_pDepthHist[nIndex] += m_pDepthHist[nIndex-1];
		}
		if (nNumberOfPoints)
		{
			for (int nIndex=1; nIndex<MAX_DEPTH; nIndex++)
			{
				m_pDepthHist[nIndex] = (unsigned int)(256 * (1.0f - (m_pDepthHist[nIndex] / nNumberOfPoints)));
			}
		}
	}

	memset(m_pTexMap, 0, m_nTexMapX*m_nTexMapY*sizeof(openni::RGB888Pixel));

	float factor[3] = {1, 1, 1};
	// check if we need to draw depth frame to texture
	if (depthFrame.isValid() && g_drawDepth)
	{
		const nite::UserId* pLabels = userLabels.getPixels();

		const openni::DepthPixel* pDepthRow = (const openni::DepthPixel*)depthFrame.getData();
		openni::RGB888Pixel* pTexRow = m_pTexMap + depthFrame.getCropOriginY() * m_nTexMapX;
		int rowSize = depthFrame.getStrideInBytes() / sizeof(openni::DepthPixel);

		for (int y = 0; y < depthFrame.getHeight(); ++y)
		{
			const openni::DepthPixel* pDepth = pDepthRow;
			openni::RGB888Pixel* pTex = pTexRow + depthFrame.getCropOriginX();

			for (int x = 0; x < depthFrame.getWidth(); ++x, ++pDepth, ++pTex, ++pLabels)
			{
				if (*pDepth != 0)
				{
					if (*pLabels == 0)
					{
						if (!g_drawBackground)
						{
							factor[0] = factor[1] = factor[2] = 0;

						}
						else
						{
							factor[0] = Colors[colorCount][0];
							factor[1] = Colors[colorCount][1];
							factor[2] = Colors[colorCount][2];
						}
					}
					else
					{
						factor[0] = Colors[*pLabels % colorCount][0];
						factor[1] = Colors[*pLabels % colorCount][1];
						factor[2] = Colors[*pLabels % colorCount][2];
					}
//					// Add debug lines - every 10cm
// 					else if ((*pDepth / 10) % 10 == 0)
// 					{
// 						factor[0] = factor[2] = 0;
// 					}

					int nHistValue = m_pDepthHist[*pDepth];
					pTex->r = nHistValue*factor[0];
					pTex->g = nHistValue*factor[1];
					pTex->b = nHistValue*factor[2];

					factor[0] = factor[1] = factor[2] = 1;
				}
			}

			pDepthRow += rowSize;
			pTexRow += m_nTexMapX;
		}
	}
*/

		////////////////

	/*
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_nTexMapX, m_nTexMapY, 0, GL_RGB, GL_UNSIGNED_BYTE, m_pTexMap);

	// Display the OpenGL texture map
	glColor4f(1,1,1,1);

	glEnable(GL_TEXTURE_2D);
	glBegin(GL_QUADS);

	g_nXRes = depthFrame.getVideoMode().getResolutionX();
	g_nYRes = depthFrame.getVideoMode().getResolutionY();

	// upper left
	glTexCoord2f(0, 0);
	glVertex2f(0, 0);
	// upper right
	glTexCoord2f((float)g_nXRes/(float)m_nTexMapX, 0);
	glVertex2f(GL_WIN_SIZE_X, 0);
	// bottom right
	glTexCoord2f((float)g_nXRes/(float)m_nTexMapX, (float)g_nYRes/(float)m_nTexMapY);
	glVertex2f(GL_WIN_SIZE_X, GL_WIN_SIZE_Y);
	// bottom left
	glTexCoord2f(0, (float)g_nYRes/(float)m_nTexMapY);
	glVertex2f(0, GL_WIN_SIZE_Y);

	glEnd();
	glDisable(GL_TEXTURE_2D);
	*/

//	drawImage(images.at(0));

	const nite::UserMap& userLabels = userTrackerFrame.getUserMap();
	const nite::Array<nite::UserData>& users = userTrackerFrame.getUsers();

	/*
		alle erkannte user analysieren.
		ist ein user neu, wird er gespeichert.
		Dieser User wird ab sofort getrackt und die Bildgrenze folgt ihm
	*/

	for (int i = 0; i < users.getSize(); ++i)
	{
		const nite::UserData& user = users[i];

		updateUserState(user, userTrackerFrame.getTimestamp());
		
		if(user.isNew())
		{
			m_pUserTracker->startSkeletonTracking(user.getId());
		}
		else if (!user.isLost())
		{
			/*
				Skelett aufrufen
				analysieren
			*/
			if(user.getId() == latestUserID)
			{
				if (users[i].getSkeleton().getState() == nite::SKELETON_TRACKED )
				{
					TrackUser(m_pUserTracker,user);
				}
			}
		}
	}

	/*
		Überlagertes Bild zeichnen
	*/
	if(	imagePairs.size()>0 && 
		imagePairID>=0 && 
		imagePairID<imagePairs.size() && 
		imageBorder>=0 && 
		imageBorder<=GL_WIN_SIZE_X)
	{

		int w=imagePairs.at(imagePairID).overlay.width;

		glEnable(GL_TEXTURE_2D);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); //GL_NEAREST = no smoothing
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(
			GL_TEXTURE_2D,			//targte
			0,						//level
			4,						//internal format
			w,						//breite 
			imagePairs.at(imagePairID).overlay.height,						//hoehe
			0,						//border
			GL_RGBA,					//format
			GL_UNSIGNED_BYTE,		//typ
			&imagePairs.at(imagePairID).overlay.img[0]				//zeiger auf 1. pixel
		);

		glEnable(GL_TEXTURE_2D);
		glBegin(GL_QUADS);

			glTexCoord2f( 0.0f, 0.0f); 
			glVertex2f( 0, 0);
      
			glTexCoord2f( 
				(float)imageBorder/(float)w, 
				0.0f); 
			glVertex2f( 
				imageBorder, 
				0);
      
			glTexCoord2f( 
				(float)imageBorder/(float)w, 
				1.0f); 
			glVertex2f( 
				imageBorder, 
				GL_WIN_SIZE_Y);
      
			glTexCoord2f( 0.0f, 1.0f); 
			glVertex2f( 
				0, 
				GL_WIN_SIZE_Y);

		glEnd();

		glDisable(GL_TEXTURE_2D);
	
	}

	// Swap the OpenGL display buffers
	glutSwapBuffers();

}

void SampleViewer::OnKey(unsigned char key, int /*x*/, int /*y*/)
{
	if(key==27)
	{
		printf("Programm was stopped by User.\n");
		exit(1);
	}
}

openni::Status SampleViewer::InitOpenGL(int argc, char **argv)
{
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
	glutInitWindowSize(GL_WIN_SIZE_X, GL_WIN_SIZE_Y);
	glutCreateWindow (m_strSampleName);
	if(g_fullScreen)
		glutFullScreen();
	glutSetCursor(GLUT_CURSOR_NONE);

	InitOpenGLHooks();

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);

	glEnableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	return openni::STATUS_OK;

}

void SampleViewer::InitOpenGLHooks()
{
	glutKeyboardFunc(glutKeyboard);
	glutDisplayFunc(glutDisplay);
	glutIdleFunc(glutIdle);
}

