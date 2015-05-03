/*
 * TMX2MAP ad hoc tool for World Rally maps+events generation
 * (based in TMX2BIN)
 * Coded by theNestruo
 *
 * Tiled (c) 2008-2013 Thorbjørn Lindeijer [http://www.mapeditor.org/]
 *
 * Version history:
 * 12/11/2013  v0.5  "tileset" tag read
 * 15/04/2013  v0.4  Bugfixing
 * 08/04/2013  v0.3  Special events
 * 03/04/2013  v0.2  Maps+events
 * 31/03/2013  v0.1  Initial version
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VALID_MAP_WIDTH (32)
#define VALID_MAP_HEIGHT (32)

/* Number of non mirrored tiles */
#define MAX_TILES (104)

#define MAX_EVENTS (32)

/* Tiled coords to chars ratio */
#define X_Y_OBJECT_SCALE (2)

#define EVENT_L (0x01)
#define EVENT_UL (0x02)
#define EVENT_U (0x03)
#define EVENT_UR (0x04)
#define EVENT_R (0x05)

#define EVENT_FINISH (0x80)
#define EVENT_JUMP (0x40)
#define EVENT_SKID (0x20)

/* Data structures ------------------------------------- */

typedef unsigned char byte;
typedef unsigned short int word;

/* Configuration parameters */
struct stConfiguration {
	char *tmxFilename;
	char *mapFilename;
};

/* TMX container */
struct stObject {
	int gid;
	int x;
	int y;
};

struct stTmx {
	int firstTileGid;
	int firstObjectGid;
	int width;
	int height;
	byte *data;
	int objectCount;
	struct stObject *objects;
};

struct stEvent {
	byte triggerType;
	byte cp;
	byte type;
	byte color;
};

/* Global constants ------------------------------------ */
byte triggerTypeByGid[] = {
	/* 0: specials: finish */
	EVENT_U | EVENT_FINISH,
	/* 1: specials: jump */
	EVENT_L | EVENT_JUMP, EVENT_U | EVENT_JUMP, EVENT_R | EVENT_JUMP,
	/* 4..: specials: skid */
	EVENT_L | EVENT_SKID, EVENT_U | EVENT_SKID, EVENT_R | EVENT_SKID,
	/* 7..: narrow */
	EVENT_L, EVENT_L, EVENT_UL, EVENT_UL, EVENT_UL,
	EVENT_UR, EVENT_UR, EVENT_UR, EVENT_R, EVENT_R,
	/* 17..: very easy */
	EVENT_L, EVENT_L, EVENT_L, EVENT_L, EVENT_UL, EVENT_UL, EVENT_UL,
	EVENT_UL, EVENT_U, EVENT_U, EVENT_U, EVENT_U, EVENT_UR, EVENT_UR, EVENT_UR,
	EVENT_UR, EVENT_R, EVENT_R, EVENT_R, EVENT_R,
	/* 37..: easy */
	EVENT_UL, EVENT_UL, EVENT_UR, EVENT_UR,
	/* 41..: medium */
	EVENT_R, /* R-U-UR: myhippa, laajavuori */
	EVENT_L, /* L-R-UR: moulinon */
	EVENT_UL, /* UL-UR-UL: konivouri */
	EVENT_UR, /* UR-UL-UR: laajavuori */
	EVENT_UR, /* UR-L-UR: laajavuori */
	EVENT_U, /* 6 */
	EVENT_U, /* 7 */
	EVENT_U, /* 8 */
	EVENT_U, /* 9 */
	/* 50..: hard */
	EVENT_UR, /* UR-L-U: konivouri */
	EVENT_UR, /* UR-R-U-UR: konivouri */
	EVENT_UR, /* UR-R-UL-U: laajavuori */
	EVENT_U, /* U-UL-R-UR: laajavuori */
	EVENT_UR /* UR-U-R-U-R: laajavuori */
};

/* Global variables ------------------------------------ */

struct stConfiguration cfg;
struct stTmx tmx;

/* Function prototypes --------------------------------- */

int readTmx(FILE *file);
char* readProperty(char *tag, char *propertyName);

int generateMap(FILE *file);
byte tileValueFrom(byte b);
struct stEvent eventFrom(struct stObject *object);

void finish();

/* Entry point ----------------------------------------- */

int main(int argc, char **argv) {

	/* debug code
	printf("tmx2map v0.5.\n");
	 */

	/* Parse command line */
	if (argc != 3) {
		printf("ERROR: Wrong usage.\n");
		return 1;
	}
	cfg.tmxFilename = argv[1];
	cfg.mapFilename = argv[2];

	/* Read TMX into global bitmap */
	FILE *tmxFile = fopen(cfg.tmxFilename, "rt");
	int i = readTmx(tmxFile);
	fclose(tmxFile);
	if (i) {
		finish();
		return i;
	}
	
	/* Generate map and events files */
	FILE *mapFile = fopen(cfg.mapFilename, "wb");
	i = generateMap(mapFile);
	fclose(mapFile);

	finish();
	return i;
}

/* Function bodies ------------------------------------- */

/*
 * Read the TMX file into the global container
 * @param file to be read
 * @return int non zero if failure
 */
int readTmx(FILE *file) {

	int i = 0;

	/* Allocate buffer */
	int bufferSize = 1024; /* should be enough (for control lines) */
	char *buffer = malloc(bufferSize), *line;
	
	/* Reads the header: XML */
	if (!(line = fgets(buffer, bufferSize, file))) {
		printf("ERROR: Could not read XML header.\n");
		i = 3;
		goto out;
	}
	if (line != strstr(line, "<?xml")) {
		printf("ERROR: TMX file is not XML.\n");
		i = 4;
		goto out;
	}
	
	/* Reads the header: TMX */
	if (!(line = fgets(buffer, bufferSize, file))) {
		printf("ERROR: Could not read TMX header.\n");
		i = 5;
		goto out;
	}
	if (line != strstr(line, "<map")) {
		printf("ERROR: TMX file is not an TMX file.\n");
		i = 6;
		goto out;
	}
	
	int j;
	for(j = 0; j < 2; j++) {
		/* Searches for the tag "tileset" */
		for(;;) {
			if (!(line = fgets(buffer, bufferSize, file))) {
				printf("ERROR: Missing <tileset> tag.\n");
				i = 7;
				goto out;
			}
			if ((line = strstr(line, "<tileset"))) {
				break; /* found! */
			}
		}
		char *firstgid;
		if (!(firstgid = readProperty(line, "firstgid"))) {
			printf("ERROR: Invalid tileset: Missing properties.\n");
			i = 8;
			goto out;
		}
		switch	(j) {
		case 0: tmx.firstTileGid = atoi(firstgid); break;
		case 1: tmx.firstObjectGid = atoi(firstgid); break;
		}
	}
		
	/* Searches for the tag "layer" */
	for(;;) {
		if (!(line = fgets(buffer, bufferSize, file))) {
			printf("ERROR: Missing <layer> tag.\n");
			i = 7;
			goto out;
		}
		if ((line = strstr(line, "<layer"))) {
			break; /* found! */
		}
	}
		
	/* Read layer properties */
	char *height, *width, *name, *encoding;
	if ((!(height = readProperty(line, "height")))
			|| (!(width = readProperty(line, "width")))
			|| (!(name = readProperty(line, "name")))) {
		printf("ERROR: Invalid layer: Missing properties.\n");
		i = 8;
		goto out;
	}
	if (((tmx.width = atoi(width)) != VALID_MAP_WIDTH)
			|| ((tmx.height = atoi(height)) != VALID_MAP_HEIGHT)) {
		printf("ERROR: Invalid width and/or height.\n");
		i = 9;
		goto out;
	}

	/* Searches for the tag "data" */
	if (!(line = fgets(buffer, bufferSize, file))) {
		printf("ERROR: Unexpected EOF.\n");
		i = 10;
		goto out;
	}
	if (!(line = strstr(line, "<data"))) {
		printf("ERROR: Missing <data> tag.\n");
		i = 11;
		goto out;
	}
	if (!(encoding = readProperty(line, "encoding"))) {
		printf("ERROR: Missing encoding property.\n");
		i = 12;
		goto out;
	}
	if (strcmp(encoding, "csv")) {
		printf("ERROR: Invalid encoding \"%s\".\n", encoding);
		i = 13;
		goto out;
	}
	
	/* Allocate space for the data */
	bufferSize = tmx.width * 4 + 16;
	buffer = realloc(buffer, bufferSize); /* "nnn," per byte and some margin */
	tmx.data = (byte*) malloc(tmx.width * tmx.height);
	
	int y;
	byte *dest = tmx.data;
	for (y = 0; y < tmx.height; y++) {
		if (!(line = fgets(buffer, bufferSize, file))) {
			printf("ERROR: Unexpected EOF.\n");
			i = 14;
			goto out;
		}
		
		int x, val;
		char *token;
		for (x = 0, token = strtok(line, ",");
				x < tmx.width;
				x++, token = strtok(NULL, ","), dest++) {
			if (!token) {
				printf("ERROR: Missing/invalid value at %d,%d.\n", x, y);
				i = 15;
				goto out;
			}
			val = atoi(token);
			if (val > 255) {
				printf("WARNING: Byte overflow at %d,%d: %d.\n", x, y, val);
			}
			*dest = (byte) val;
		}
	}
	
	/* Searches for the tag "objectgroup" */
	for(;;) {
		if (!(line = fgets(buffer, bufferSize, file))) {
			printf("ERROR: Missing <objectgroup> tag.\n");
			i = 16;
			goto out;
		}
		if ((line = strstr(line, "<objectgroup"))) {
			break; /* found! */
		}
	}

	/* Searches for the first tag "object" */
	if (!(line = fgets(buffer, bufferSize, file))) {
		printf("ERROR: Unexpected EOF.\n");
		i = 17;
		goto out;
	}
	if (!(line = strstr(line, "<object"))) {
		printf("ERROR: Missing <object> tag.\n");
		i = 18;
		goto out;
	}
	
	/* Allocate space for the first object */
	tmx.objectCount = 1;
	tmx.objects = (struct stObject*) malloc(sizeof(struct stObject));
	struct stObject *currentObject = tmx.objects;
	for(;;) {
		/* Read object properties */
		char *gid, *x, *y;
		if ((!(y = readProperty(line, "y")))
				|| (!(x = readProperty(line, "x")))
				|| (!(gid = readProperty(line, "gid")))) {
			printf("ERROR: Invalid object: Missing properties.\n");
			i = 19;
			goto out;
		}
		if ((!(currentObject->gid = atoi(gid)))
				|| (!(currentObject->x = atoi(x)))
				|| (!(currentObject->y = atoi(y)))) {
			printf("ERROR: Invalid gid, x and/or y.\n");
			i = 20;
			goto out;
		}
		
		/* Searches for the next tag "object" */
		if (!(line = fgets(buffer, bufferSize, file))) {
			printf("ERROR: Unexpected EOF.\n");
			i = 21;
			goto out;
		}
		if (strstr(line, "</objectgroup")) {
			break;
		}
		if (!(line = strstr(line, "<object"))) {
			printf("ERROR: Missing <object> tag.\n");
			i = 22;
			goto out;
		}
	
		/* Allocate space for the next object */
		if (++tmx.objectCount > MAX_EVENTS) {
			printf("ERROR: Too many objects.\n");
			i = 23;
			goto out;
		}
		tmx.objects = realloc(tmx.objects, tmx.objectCount * sizeof(struct stObject));
		currentObject++;
	}
	
out:
	/* Success/fail message */
	printf(i ? "ERROR reading TMX file.\n" : "TMX file read.\n");
	free(buffer);
	return i;
}

/**
 * Extracts the value of a property of a given tag.
 * @param tag where the property will be searched
 * Warning! The value of the tag will be modified
 * @param propertyName the name of the property
 * @ret pointer to the value of the tag
 */
char* readProperty(char *tag, char *propertyName) {

	char *property, *from, *to; 
	if (!(property = strstr(tag, propertyName))) {
		return NULL;
	}
	if (!(from = strstr(property, "\""))) {
		return NULL;
	}
	from++;
	if (!(to = strstr(from, "\""))) {
		return NULL;
	}
	to[0] = '\0';
	
	return from;
}

int generateMap(FILE *file) {

	int i = 0;
	
	/* Map */
	int j;
	for (j = 0; j < tmx.width * tmx.height; j++) {
		byte value = tileValueFrom(tmx.data[j]);
		if (fwrite(&value, 1, 1, file) != 1) {
			i = 24;
			goto out;
		}
	}
	
	/* Events */
	for (j = 0; j < tmx.objectCount; j++) {
		struct stEvent event = eventFrom(&tmx.objects[j]);
		
		/* debug code
		printf("from: %d @ %d,%d to: trigger %d, cp %d, type %d, color %d\n",
			tmx.objects[j].gid, tmx.objects[j].x, tmx.objects[j].y,
			event.triggerType, event.cp, event.type, event.color);
		 */
		
		if (fwrite(&event, sizeof(struct stEvent), 1, file) != 1) {
			i = 25;
			goto out;
		}
	}
	
out:
	/* Success/fail message */
	printf(i ? "ERROR writing map file.\n" : "Map file written.\n");
	return i;
}

byte tileValueFrom(byte b) {

	/* Normal */
	if (b <= MAX_TILES) {
		byte value = (byte) (b - tmx.firstTileGid);
		return value;
	}
	
	/* Mirrored */
	byte x = (b - tmx.firstTileGid - MAX_TILES) % 8;
	byte y = (byte) ((b - tmx.firstTileGid - MAX_TILES) / 8);
	byte value = 8*y + (7 - x) + 128;
	return value;
}

struct stEvent eventFrom(struct stObject *object) {

	struct stEvent event;
	byte gid = (byte) (object->gid - tmx.firstObjectGid);
	byte x = (byte) (object->x / X_Y_OBJECT_SCALE);
	byte y = (byte) (object->y / X_Y_OBJECT_SCALE);
	
	/* Trigger type and cp value */
	switch((event.triggerType = triggerTypeByGid[gid]) & 0x07) {
	case EVENT_L:
	case EVENT_R:
		event.cp = x;
		break;
	case EVENT_UL:
		event.cp = (byte) ((y + x) & 0xff);
		break;
	case EVENT_U:
		event.cp = y;
		break;
	case EVENT_UR:
		event.cp = (byte) ((y - x + 0x100) & 0xff);
		break;
	}
	
	if (gid < 7) {
		/* Special events */
		event.color = 0;
		event.type = 0;
			  // (gid == 0) ?	event.triggerType | 0x80 /* finish */
			// : (gid < 4) ?	event.triggerType | 0x40 /* jump */
			// :		event.triggerType | 0x20; /* skid */
			
	} else {
		/* Normal events */
		event.color =
			  (gid < 17) ?	0 /* narrow */
			: (gid < 37) ?	1 /* very easy */
			: (gid < 41) ?	2 /* easy */
			: (gid < 50) ?	3 /* medium */
			:		4; /* hard */
		event.type = gid - 7;
	}
	
	return event;
}

/*
 * Exit gracefully
 */
void finish() {

	if (tmx.data) {
		free(tmx.data);
		tmx.data = NULL;
	}
	if (tmx.objects) {
		free(tmx.objects);
		tmx.objects = NULL;
	}
}
