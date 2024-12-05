#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <stdarg.h> // variadic function which takes undefined number of arguments: writeTolog()
#include <getopt.h> //compilation option -R for recursive exploring
#include <sys/mman.h> //mmap
#include <errno.h>
#include <signal.h>
#include <utime.h>
#include <sys/time.h>

void daemonize(int log);
void writeToLog(int log, char* format, ...);
int isDirectory(const char *path);
int isRegularFile(const char *path);
int fileExists(char *path);
void copyFile(char *srcPath, char *destPath, size_t fileSize, long int sizeThreshold, int log);
static time_t getFileModificationTime(const char *path);
void updateModificationTime(const char *path, time_t srcTime);
void deleteDirectoryTree(char* path, int log);
void explore(char *srcDirectory, char *destDirectory, int log, int recursionFlag, long int sizeThreshold);



int main(int argc, char *argv[]) {

    int opt;
    int recursionFlag = 0, sleepValue = 300, sizeThreshold = 100000;
    int daemonLog = open("daemonLog.txt", O_CREAT | O_WRONLY | O_TRUNC);

    writeToLog(daemonLog, "\nSRC: %s\nDEST: %s\n", argv[argc-2], argv[argc-1]);
    while((opt=getopt(argc,argv, "RS:B:")) != -1) {
        switch(opt) {
            case 'R':
                recursionFlag = 1;
                writeToLog(daemonLog, "\n               RECURSION                    \n");
                break;
            case 'S':
                sleepValue = atoi(optarg);
                writeToLog(daemonLog, "\n               CUSTOM SLEEP                    \n");
                break;
            case 'B':
                sizeThreshold = atoi(optarg);
                writeToLog(daemonLog,"\n                CUSTOM SIZE             \n");
                break;
             default:
                break;
        }
    }
    if(isDirectory(argv[argc-2]) == 0 || isDirectory(argv[argc-1]) == 0) { //check if arguments are directories,
        writeToLog(daemonLog, "\nERROR: ARGUMENTS ARE NOT DIRECTORIES\n");
        close(daemonLog);
        exit(EXIT_FAILURE);
    } else {
        daemonize(daemonLog);
        while(1) {

            writeToLog(daemonLog, "\nENTER INFINITE LOOP\n");
            writeToLog(daemonLog, "DAEMON SLEEP %d sec.\n", sleepValue);
            sleep(sleepValue);
            writeToLog(daemonLog, "DAEMON WOKE UP\n");

            explore(argv[argc-2],argv[argc-1], daemonLog, recursionFlag, sizeThreshold); // Reading/synchronizing directories
        }
    }
}

void writeToLog(int log, char* format, ...) {
    size_t bytes;
    char buffer[1024];
    va_list argPtr;
    va_start(argPtr, format);
    vsnprintf(buffer,1024, format, argPtr);
    bytes = strlen(buffer);
    write(log, buffer, bytes);
    va_end(argPtr);
}

void daemonize(int log) {
    pid_t pid=0, sid=0; //process ID and session ID

        pid = fork();

        // Error checking
        if(pid < 0) {
            writeToLog(log, "\nERROR: PROCESS ID\n");
            close(log);
            _exit(EXIT_FAILURE);
        }
        else if(pid > 0) {
            _exit(EXIT_SUCCESS);
        }
        umask(0);
        writeToLog(log, "\nDAEMON STARTED SUCCESSFULLY\n");

        sid = setsid();

        // Error checking
        if(sid<0) {
            writeToLog(log, "\nERROR: SESSION ID");
            close(log);
            exit(EXIT_FAILURE);
        }
        // Change current working directory
        chdir("/");
        // Error checking
        if(chdir("/") < 0) {
            writeToLog(log, "\nERROR: CHANGING DIRECTORY FAILED");
            close(log);
            exit(EXIT_FAILURE);
        }
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
}

void copyFile(char *srcFilePath, char *destFilePath, size_t fileSize,long int sizeThreshold, int log) {
    int src, dest;
    long int bufferSize; 
    char *buffer;
    size_t bytesRead;

    src = open(srcFilePath, O_RDONLY);
    dest = open(destFilePath, O_CREAT | O_WRONLY);

    if(fileSize >= sizeThreshold) {
        buffer = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, src,0);
        writeToLog(log, "\nMAPPING FILE...\n");
        write(dest, buffer, fileSize);
    } else {
        bufferSize = 100;
        buffer = malloc(bufferSize*sizeof(char));
        writeToLog(log, "\n\n WRTING FILE...\n");

        while ((bytesRead = read(src, buffer, bufferSize)) > 0)
        write(dest, buffer, bytesRead);

    }

    while ((bytesRead = read(src, buffer, bufferSize)) > 0) 
        write(dest, buffer, bytesRead);
    
    close(dest);
    close(src);
}

//returns non zero value if path is directory
int isDirectory(const char *path) {
    struct stat sb;     //<sys/stat.h>
    stat(path, &sb);
    return S_ISDIR(sb.st_mode);
}

int isRegularFile(const char *path) {
    struct stat sb;  //<sys/stat.h>
    stat(path, &sb);
    return S_ISREG(sb.st_mode);
}

int fileExists (char *path)
{
  struct stat sb;   
  return (stat (path, &sb) == 0);
}

static time_t getFileModificationTime(const char *path) {
    struct stat sb;
    if (stat(path, &sb) == 0)
        return sb.st_mtime;
    return 0;
}

void updateModificationTime(const char *path, time_t srcTime) {
    struct stat sb;
    struct utimbuf ub;
    if(stat(path, &sb)==0)
        sb.st_mtime = srcTime;
    if (utime(path, &ub) == 0) {
        srcTime = sb.st_mtime;
    
        ub.actime = sb.st_atime; // keep atime unchanged 
        ub.modtime = srcTime;   // set mtime to source file time
        utime(path,&ub);
    }
}

void deleteDirectoryTree(char* path, int log) {
    DIR*            dest;
    struct dirent*  entry;
    char            newPath[1024] = {0};
    dest = opendir(path);

    if(!dest) {
        writeToLog(log, "\nERROR: DIRECTORY STREAM\n");
        close(log);
        exit(EXIT_FAILURE);
    }
    while ((entry = readdir(dest)) != NULL) {
        if (strcmp(entry->d_name, ".")==0 || strcmp(entry->d_name, "..")==0)
            continue;
        sprintf(newPath, "%s/%s", path, entry->d_name);
        if (isDirectory(newPath)) {
            deleteDirectoryTree(newPath,log);
        } else {
            unlink(newPath);
        }
    }
    closedir(dest);
    rmdir(path);
}

void explore(char *srcDirectory, char *destDirectory, int log, int recursionFlag, long int sizeThreshold) {
    
    DIR *src = NULL, *dest = NULL; // directory pointer, DIR object

    src = opendir(srcDirectory);       // create source directory stream
    dest = opendir(destDirectory);      // create source directory stream
        // directory stream, opendir returns DIR object, <sys/types.h>, <dirent.h>
        // A directory stream is little more than a file descriptor representing the open directory,
        // some metadata, and a buffer to hold the directory's contents.
        // Consequently, it is possible to obtain the file descriptor behind a given directory stream:
        // function for obtaining file descriptor: int dirfd (DIR *dir);

        // Error checking
    if(!src && !dest) {
        writeToLog(log, "\nERROR: DIRECTORY STREAM");
        close(log);
        exit(EXIT_FAILURE);
    }
        // if directory stream created successfully, we can read entries from directory
        // struct dirent * readdir (DIR *dir);  - returns entries one by one from given DIR object
        // readdir() obtains entries one by one, untill the one we are searching for is found
        // or the entire directory is read, at which time it returns NULL

        // struct 'dirent' defined in <dirent.h>
        // 'd_name' field in structure is the name of single file within the directory
    struct  dirent *srcEntry, *destEntry; // entries in directories
    char srcFilePath[1024], destFilePath[1024];
    char srcFileName[1024], destFileName[1024];
    struct stat sb;
    char srcRecursionPath[1024], destRecursionPath[1024];

    while( (destEntry = readdir(dest)) != NULL ) {

        writeToLog(log, "READING DESTINATION ENTRIES #2...\n");
        if(destEntry->d_type == DT_REG) {
            snprintf(destFileName, 1024, "%s", destEntry->d_name);
            snprintf(destFilePath,1024,"%s/%s", destDirectory, destFileName);

            if(stat(destFilePath, &sb) != -1) {
                writeToLog(log, "\nDirectory:      %s\nFile:           %s\nFile Full Path: %s\nSize:           %ld\n",
                    destDirectory, destFileName, destFilePath, sb.st_size);
            }

            writeToLog(log, "CHECKING IF FILE EXISTS IN SOURCE DIRECTORY #2...\n");
            snprintf(srcFilePath,1024,"%s/%s", srcDirectory, destFileName);
            writeToLog(log, "\nSOURCE FILE: #1... %s\n", destFilePath);
                        ///////////////////////////////////////////
            if (fileExists(srcFilePath) ) {
                writeToLog(log, "\n\n\n\n\n\n\n\n\n\n\n FILE REALLY EXISTS #2... %s\n\n\n", destFilePath);
                if(isRegularFile(destFilePath)) {

                        }
            } else {
                writeToLog(log, "\n\n\n\n\n\n\n\n\n\n\n FILE DOESNT EXIST, REMOVING #2... %s\n\n\n", destFilePath);
                writeToLog(log, "\nDELETING FILES #2\n");
                        
                int status;
                status = unlink(destFilePath);
                if(status == 0) {
                    writeToLog(log, "\nFILE DELETED SUCCESSFULLY");
                } else {
                    writeToLog(log, "\nERROR: UNABLE TO DELETE THE FILE");
                    writeToLog(log,"\n\n****\n\n\nERROR:  unlink()! %s\n", strerror(errno));
                    close(log);
                    exit(EXIT_FAILURE);
                }
            }
        } else if(destEntry->d_type == DT_DIR && recursionFlag == 1) {
            if (strcmp(destEntry->d_name, ".") == 0 || strcmp(destEntry->d_name, "..") == 0)
                continue;
            char destDirectoryRemove[1024];
            char srcDirectoryExists[1024];

            snprintf(destDirectoryRemove,1024,"%s/%s", destDirectory, destEntry->d_name);
            snprintf(srcDirectoryExists,1024,"%s/%s", srcDirectory, destEntry->d_name);
            
            if(isDirectory(srcDirectoryExists) == 0){
                deleteDirectoryTree(destDirectoryRemove, log);
            }
        } else 
            continue;
        //     struct stat st = {0};
        //     snprintf(srcRecursionPath,1024,"%s/%s", srcDirectory, srcEntry->d_name);
        //     snprintf(destRecursionPath,1024,"%s/%s", destDirectory, srcEntry->d_name);
        //     if (stat(destRecursionPath, &st) == -1) {
        //         mkdir(destRecursionPath, 0777);
        //     }
        //     explore(srcRecursionPath,destRecursionPath, log, recursionFlag);
        // } else {
        //     continue;
        // }
    }
    rewinddir(src);
    rewinddir(dest);

    while( (srcEntry = readdir(src)) != NULL ) {

        writeToLog(log, "READING SOURCE ENTRIES #1...\n");
        if(srcEntry->d_type == DT_REG) {
            snprintf(srcFileName, 1024, "%s", srcEntry->d_name);
            snprintf(srcFilePath,1024,"%s/%s", srcDirectory, srcFileName);
                    
            if(stat(srcFilePath, &sb) != -1) {
                writeToLog(log, "\nDirectory:      %s\nFile:           %s\nFile Full Path: %s\nSize:           %ld\n",
                srcDirectory, srcFileName, srcFilePath, sb.st_size);
            }
            writeToLog(log, "CHECKING IF FILE EXISTS IN DESTINATION DIRECTORY #1...\n");
            snprintf(destFilePath,1024,"%s/%s", destDirectory, srcFileName);
            writeToLog(log, "\nDESTINATION FILE: #1... %s\n", destFilePath);

            if (fileExists(destFilePath) ) {
                writeToLog(log, "\n\n\n\n\n\n\n\n\n\n\n FILE REALLY EXISTS #1... %s\n\n\n", destFilePath);
                if(isRegularFile(destFilePath)){ 
                    writeToLog(log, "\n\n\n\n\n\n\n\n\n\n\n FILE IS REGULAR FILE EXISTS #1... %s\n\n\n", destFilePath);
                    time_t srcTime = getFileModificationTime(srcFilePath);
                    time_t destTime = getFileModificationTime(destFilePath);
                    if(srcTime > destTime || srcTime < destTime){
                        writeToLog(log, "\n\n\n\n\n\n\n\n\n\n\n COPYING FILE  #1... %s\n\n\n", destFilePath);
                        copyFile(srcFilePath,destFilePath, sb.st_size, sizeThreshold, log);
                        updateModificationTime(destFilePath, srcTime);
                    }
                }
            } else {
                writeToLog(log, "\n\n\n\n\n\n\n\n\n\n\n FILE DOESNT EXISTS, CREATING #1... %s\n\n\n", destFilePath);
                copyFile(srcFilePath,destFilePath, sb.st_size, sizeThreshold, log);
            }
        } else if(srcEntry->d_type == DT_DIR && recursionFlag == 1) {
            if (strcmp(srcEntry->d_name, ".") == 0 || strcmp(srcEntry->d_name, "..") == 0)
                continue;
            struct stat st = {0};
            
            snprintf(srcRecursionPath,1024,"%s/%s", srcDirectory, srcEntry->d_name);
            snprintf(destRecursionPath,1024,"%s/%s", destDirectory, srcEntry->d_name);
            if (stat(destRecursionPath, &st) == -1) 
                mkdir(destRecursionPath, 0777);
        
            explore(srcRecursionPath,destRecursionPath, log, recursionFlag, sizeThreshold);
        } else 
            continue;
    }
    closedir(src);  // close source directory stream
    closedir(dest); // close destination directory stream
            //TODO: Error checking for closedir()
}