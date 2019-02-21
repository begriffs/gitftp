## GitFTP

Browse git tree and download files over anonymous FTP.

This is a tiny, portable FTP server that reads from a Git object
database rather than a filesystem.

### Usage

```bash
gitftp /path/to/repo
```

It listens on port 8021.

### How to build

* Install [libgit2](https://libgit2.org/)
* Clone this project
* Run `make`

### TODOs

This is a proof-of-concept. See the issues for ideas to make it full
fledged.  Pull requests are welcome.
