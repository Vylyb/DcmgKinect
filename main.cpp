/*******************************************************************************
*                                                                              *
*   PrimeSense NiTE 2.0 - User Viewer Sample                                   *
*   Copyright (C) 2012 PrimeSense Ltd.                                         *
*                                                                              *
*******************************************************************************/

#include "Viewer.h"

int main(int argc, char** argv)
{
	if(argc!=2)
	{
		printf("Pfad zu Definitionsdatei (.cfg) fehlt!\n");
		return 1;
	}
	openni::Status rc = openni::STATUS_OK;

	SampleViewer sampleViewer("User Viewer");

	rc = sampleViewer.Init(argc, argv);
	if (rc != openni::STATUS_OK)
	{
		return 1;
	}
	sampleViewer.Run();
}
