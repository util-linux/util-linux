/* frag.c - simple fragmentation checker
            V1.0 by Werner Almesberger
            V1.1 by Steffen Zahn, adding directory recursion
            V1.2 by Rob Hooft, adding hole counts
            V1.3 by Steffen Zahn, email: szahn%masterix@emndev.siemens.co.at
                    14 Nov 93
                    - ignore symlinks,
                    - don't cross filesys borders
                    - get filesystem block size at runtime
	    V1.4 by Michael Bischoff <mbi@mo.math.nat.tu-bs.de> to handle
	            indirect blocks better, but only for ext2fs
		    (applied by faith@cs.unc.edu, Sat Feb  4 22:06:27 1995)

            TODO: - handle hard links
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <linux/fs.h>                      /* for FIBMAP */

typedef struct StackElem {
    struct StackElem *backref, *next;
    char name[NAME_MAX];
    char dir_seen;
    char from_cmd_line;
} StackElem;

StackElem *top = NULL;


void discard( void )
{
    StackElem *se = top;
    if( se == NULL )
        return ;
    top = se->next;
    free(se);
}

void push( StackElem * se )
{
    se -> next = top;
    top = se;
}

char *p2s( StackElem *se, char *path )
{
    char *s;
    if( se->backref!=NULL ) {
        path = p2s( se->backref, path );
        if( path[-1]!='/' )
            *path++ = '/';
    }
    s = se->name;
    while( *s )
        *path++ = *s++;
    return path;
}

char *path2str( StackElem *se, char *path )
{
    *(p2s( se, path ))=0;
    return path;
}
              
void *xmalloc( size_t size )
{
    void *p;
    if( (p=malloc(size))==NULL ) {
        fprintf(stderr,"\nvirtual memory exhausted.\n");
        exit(1);
    }
    return p;
}

int main(int argc,char **argv)
{
    int fd,last_phys_block,
        fragments_in_file, blocks_in_file,
        blocks,current_phys_block,
        this_fragment, largest_fragment, i;
    long sum_blocks=0, sum_frag_blocks=0, sum_files=0, sum_frag_files=0;
    long num_hole=0, sum_hole=0, hole;
    struct stat st;
    struct statfs stfs;
    StackElem *se, *se1;
    char path[PATH_MAX], pathlink[PATH_MAX], *p;
    DIR *dir;
    struct dirent *de;
    char silent_flag=0;
    dev_t local_fs;
    int block_size;
    
    if (argc < 2)
    {
        fprintf(stderr,"usage: %s [-s [-s]] filename ...\n",argv[0]);
        exit(1);
    }
    argc--; argv++;
    while (argc>0)
    {
        p = *argv;
        if( *p=='-' )
            while( *++p )
                switch( *p )
                {
                case 's':
                    silent_flag++; /* may be 1 or 2 */
                    break;
                default:
                    fprintf(stderr,"\nunknown flag %c\n", *p );
                    exit(1);
                }
        else
        {
            se = xmalloc( sizeof(StackElem) );
            se->backref=NULL; se->dir_seen=0; se->from_cmd_line=1;
            strcpy( se->name, p );
            push(se);
        }
        argc--; argv++;
    }
    while ( top != NULL)
    {
        se = top;
        if( se->dir_seen )
            discard();
        else
        {
            path2str( se, path );
            if( readlink( path, pathlink, sizeof(pathlink) )>=0 )
            {                   /* ignore symlinks */
                if(silent_flag<1)
                {
                    printf("symlink %s\n", path );
                }
                discard();
            }
            else if( stat( path,&st) < 0)
            {
                perror( path );
                discard();
            }
            else if( !se->from_cmd_line && (local_fs!=st.st_dev) )
            {                   /* do not cross filesystem borders */
                if(silent_flag<2)
                {
                    printf("different filesystem %s\n", path );
                }
                discard();
            }
            else
            {
                if( se->from_cmd_line )
                {
                    local_fs = st.st_dev;
                    if ( statfs( path, &stfs )<0 )
                    {
                        perror( path );
                        block_size = 1024;
                    }
                    else
                        block_size = stfs.f_bsize;
                }
                if( S_ISREG(st.st_mode))   /* regular file */
                {
                    if ( (fd = open( path ,O_RDONLY)) < 0 )
                    {
                        perror( path );
                        discard();
                    }
                    else
                    {
                        last_phys_block = -1;
                        fragments_in_file = 0;
                        hole = 0; this_fragment=0;
                        largest_fragment=0;
                        blocks_in_file = (st.st_size+block_size-1)/block_size;
                        for (blocks = 0; blocks < blocks_in_file; blocks++)
                        {
                            current_phys_block = blocks;
                            if (ioctl(fd,FIBMAP,&current_phys_block) < 0)
                            {
                                perror(path);
                                break;
                            }
                            if (current_phys_block) { /* no hole here */
				int indirect;
				/* indirect is the number of indirection */
				/* blocks which must be skipped */
				indirect = 0;
				/* every 256 blocks there is an indirect block,
				   the first of these is before block 12 */
				if (blocks >= 12 && (blocks-12) % 256 == 0)
				    ++indirect;
				/* there is a block pointing to the indirect
				   blocks every 64K blocks */
				if (blocks >= 256+12 && (blocks-256-12) % 65536 == 0)
				    ++indirect;	/* 2nd indirect block */
				/* there is a single triple indirect block */
				if (blocks == 65536 + 256 + 12)
				    ++indirect;
                                if (last_phys_block == current_phys_block-1-indirect)
                                    this_fragment++;
				else { /* start of first or new fragment */
                                    if( largest_fragment<this_fragment )
                                        largest_fragment=this_fragment;
                                    this_fragment=1;
                                    fragments_in_file++;
                                }
                                last_phys_block = current_phys_block;
                            }
                            else
                            {
                                hole++;
                            }
                        }
                        if( largest_fragment<this_fragment )
                            largest_fragment=this_fragment;
                        blocks_in_file-=hole;
                                /* number of allocated blocks in file */
                        if( !silent_flag )
                        {
                            if( fragments_in_file < 2
                                || blocks_in_file < 2 )
                                i = 0; /* fragmentation 0 % */
                            else
                                i = (fragments_in_file - 1) * 100 /
                                    (blocks_in_file-1);
                                /* maximum fragmentation 100%
                                   means every block is an fragment */
                            printf(" %3d%%  %s  (%d block(s), %d fragment(s), largest %d",
                                   i, path, blocks_in_file,
                                   fragments_in_file,largest_fragment);
                            if (hole)
                            {
                                printf(", %d hole(s))\n",hole);
                            }
                            else
                            {
                                printf(")\n");
                            }
                        }
                        sum_blocks+=blocks_in_file;
                        if (hole)
                            num_hole++;
                        sum_hole+=hole;
                        sum_files++;
                        if( fragments_in_file>1 )
                        {
                            sum_frag_blocks+=blocks_in_file-largest_fragment;
                            sum_frag_files++;
                        }
                        discard();
                        close(fd);
                    }
                }
                else if( S_ISDIR( st.st_mode ) ) /* push dir contents */
                {
                    if( (dir=opendir( path ))==NULL )
                    {
                        perror(path);
                        discard();
                    }
                    else
                    {
                        if( silent_flag<2 )
                            printf("reading %s\n", path);
                        while( (de=readdir(dir))!=NULL )
                        {
                            if( (strcmp(de->d_name,".")!=0)
                                && (strcmp(de->d_name,"..")!=0) )
                            {
                                se1 = xmalloc( sizeof(StackElem) );
                                se1->backref=se; se1->dir_seen=0;
                                se1->from_cmd_line=0;
                                strcpy( se1->name, de->d_name );
                                push(se1);
                            }
                        }
                        closedir( dir );
                        se->dir_seen=1;
                    }
                }
                else /* if( S_ISREG(st.st_mode)) */
                    discard();
            }
        } /* if( se->dir_seen ) */
    } /* while ( top != NULL) */
    if (sum_files>1)
    {
        printf("\nsummary:\n");
        printf(" %3ld%% file  fragmentation (%ld of %ld files contain fragments)\n",
               sum_files<1 ? 0L : sum_frag_files*100/sum_files,
               sum_frag_files, sum_files);
        printf(" %3ld%% block fragmentation (%ld of %ld blocks are in fragments)\n",
               sum_blocks<1 ? 0L : sum_frag_blocks*100/sum_blocks,
               sum_frag_blocks, sum_blocks);
        if (num_hole>1)
            printf("  %ld files contain %ld blocks in holes\n",
                   num_hole,sum_hole);
    }
    exit(0);
}
