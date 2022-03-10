# Command Line Documentation

## Usage

``` yaml
ce <COMMAND> <mandatory-args> [--switches]

Commands:

  help [COMMAND]                : get help for a command (or general help without the command).
  version [--check] [--update]  : manage the version of ce


General Switches:

  --help          : alternative use for getting help 
  --debug         : enable debugging 
  --force         : do the requested action without confirmation
  --language=<LL> : use the given localization language code when running the application (ie, en, de, ... etc)
  --home=<folder> : use the given folder as the $VCPKG_ROOT folder 
  --what-if       : explain what would happen without doing the command

  
    
    
Notes: 

  $VCPKG_ROOT folder is determined by:
    - command line (--ce-home, --VCPKG_ROOT)
    - environment (VCPKG_ROOT)
    - default 1 $HOME/.vcpkg
    - default 2 <tmpdir>/.vcpkg
```


# Informational commands
Informational refers to the features that help the user get started/familiar with
the tooling.


### Get tool version infomation  
``` bash
ce version
```

__Expected Output__
``` bash
ce Version Information: 
  Core: 1.2.3 
  Node: 14.1.5
```


### Get help about a given command
``` bash 
ce help [command]
```

__Parameters__
|Parameter| Description| 
|---------|------------|
|`[command]` | A specific command to get help about<br>will return a list of commands and summaries if not specifed |

__Expected Output__  
General command line infomation, or specific information regarding the command specified.

### Check if there is an update available

``` bash 
ce check-version
```

__Expected output__

``` bash
# scenario 1 - a version update is available
> ce check-version
* NOTE: an upgrade to ce is available (version 1.2.3). 

# scenario 2 - no update is available
> ce check-version
* NOTE: ce is up-to-date with the latest version (1.1.0)

# scenario 3 - failure (offline, no-access, etc)
> ce check-version
* NOTE: unable to check for latest ce version.

```

# Artifact Discovery/Installation commands
Discovery refers to the features that enable consumers to find the artifacts they are 
interested in.

### Finding an artifact in an artifact repository

Command 
``` bash 
> ce find <artifact...>
```

__Parameters__
|Parameter| Description| 
|---------|------------|
|`<artifact...>` | one or more words in a artifact identity, multiple words narrow the search  |

__options__
| Option  | Description| 
|---------|------------|
|`--source <source-name>` |  use a specific artifact source (repository) | 


### Installs an artifact given an identity
``` bash
ce install <artifact-identity> 
```
__Parameters__
|Parameter| Description| 
|-|-|
| `<artifact-identity>` | either a fully-qualified artifact identity, or a artifact identity short name<br>see [Artifact Identities](./#Artifact-Identities) |

__options__
| Option  | Description| 
|---------|------------|
|`--version <version-or-range>` | a specific version or version range to install.<br>see [Versions](#versions)  |

### Remove an installed artifact 

``` bash 
ce uninstall <artifact-identity> 
```

__Parameters__
|Parameter| Description| 
|-|-|
| `<artifact-identity>` | either a fully-qualified artifact identity, or a artifact identity short name<br>see [Artifact Identities](./#Artifact-Identities) |

__options__
| Option  | Description| 
|---------|------------|
|`--version <version-or-range>` | a specific version or version range to install.<br>see [Versions](#versions)  |

### List installed artifacts

Lists the artifacts installed into the system

``` bash 
ce list --global 
```

__Expected output__

```
Artifact          Version     Description
----------------- ----------- --------------------------
compilers/gnu/gcc 1.2.3       gnu gcc compiler
tools/cmake       2.3.4       cmake tool

```


# Management of artifcats into a project

These commands modify a project allowing the user to use artifacts and store the references to them in the project profile. 

## Add artifact to project


'Adding' an artifact means to 
 - ensure that it is installed to the local `$VCPKG_ROOT`
 - add a `requires` entry to the current project 
 - activate the settings immediately (update cmake,env,etc )


``` bash
ce add <artifact-identity>
```

__Parameters__
|Parameter| Description| 
|-|-|
| `<artifact-identity>` | either a fully-qualified artifact identity, or a artifact identity short name<br>see [Artifact Identities](./#Artifact-Identities) |

__options__
| Option  | Description| 
|---------|------------|
|`--version <version-or-range>` | a specific version or version range to add to the project.<br>see [Versions](#versions)  |

__Expected Output__

 ``` powershell
# use msvc for this project
> ce add msvc # this is the short name for compilers/microsoft/msvc

[progress information]

Installed:
  compilers/microsoft/msvc         17.0.0
  ... # whatever packages were installed.
 
Added to 'ce.yaml':
  compilers/microsoft/msvc: * 17.0.0  # this shows that it was asked to grab the latest, and it found 17.0.0
  
  #note that it does not add the dependencies to the ce.yaml file by default.

 ```



## Remove artifact from project
``` bash
ce remove <artifact-identity>
```


__Parameters__
|Parameter| Description| 
|-|-|
| `<artifact-identity>` | either a fully-qualified artifact identity, or a artifact identity short name<br>see [Artifact Identities](./#Artifact-Identities) |

__options__
| Option  | Description| 
|---------|------------|
|`--version <version-or-range>` | a specific version or version range to remove from the project.<br>see [Versions](#versions)  |

Removing an artifact from a project removes the `requires:`  reference to the target
and performs an activation on the project again (which should remove references to the target)

This does not delete the artifact from the `$VCPKG_ROOT` folder.
This can not be used to remove artifacts that have been 'inserted' into the project (since there is no reference)

``` powershell

> ce remove boost

Removed from 'ce.yaml':
  libraries/boost
  
>
```

## Insert an artifact into a project

Insertion differs from 'add' in that instead of registering the artifact into the project and referencing it,
the whole artifact is copied to the project folder.

This does not delete the artifact from the `$VCPKG_ROOT` folder.

Activation is not done for 'inserted' artifacts, since the project intends to use them in a very particular way.

This would be inadvisable for binary tools.

``` bash
ce insert <artifact-identity>
```

__Parameters__
|Parameter| Description| 
|-|-|
| `<artifact-identity>` | either a fully-qualified artifact identity, or a artifact identity short name<br>see [Artifact Identities](./#Artifact-Identities) |

__options__
| Option  | Description| 
|---------|------------|
|`--version <version-or-range>` | a specific version or version range to insert into the project.<br>see [Versions](#versions)  |


__Expected Output__

``` powershell

c:\projects\myproj > ce insert AzureRTOS

[progress information]

Installed:
  frameworks/Azure/RTOS         4.3.2

Copied to 'c:\projects\myproj\AzureRTOS':
  frameworks/Azure/RTOS         4.3.2

```
Notes about insertion:
 - Ideally, we should support the idea of an insertion as a git submodule, so that the user *can* apply updates from the upstream.
 - in a 'copy'-style insertion, there is no ability to update, since it's just dumping files into the project. Developers are smart, they can work with that. 
 - there should probably be a way to mark things as 'insertion-friendly' and things that are not would ask for confirmation. (this would stop accidentally saying insert and having MSVC copied into your project.)


## Update an artifact in a project

Updates the artifact in the profile, installing if necessary

Updating an artifact is really a variant of `add` in that it should look for a newer version in the original
`version-or-range` and install that, removing the reference to the previous package.


## List artifacts in a project


``` bash
ce list
```

## Create a new project profile

`ce` uses a file for the project profile (`ce.yaml`) to store the configuration for a given project

there is very little information required, so a user can just create a project profile:


``` bash
ce new [project-profile.yaml] 
```

__Parameters__
|Parameter| Description| 
|-|-|
| `[project-profile.yaml]` | the file name of the project profile. defaults to `./ce.yaml` |

__options__
| Option  | Description| 
|---------|------------|
|`--id <project-identity>` | the  |
|`--version <semver>` | the version for this project. defaults to `1.0.0`  |
|`--summary <project-identity>` | the summary for this project.   |
|`--description <project-identity>` | the full description for the project |


## Activate a project profile

restores artifacts required and activates the settings

``` bash
ce activate [project-profile.yaml] 
```

__Parameters__
|Parameter| Description| 
|-|-|
| `[project-profile.yaml]` | the file name of the project profile. defaults to `./ce.yaml` |



# Management of artifcats sources

## Add an artifact source 

``` bash
ce add-source <name> <location>
```


## Remove an artifact source 

``` bash
ce remove-source <name>
```

<hr>

## Artifact Identities

Artifact identities are specified in one of the following forms:

`full/identity/path` - the full identity of an artifact that is in the built-in artifact source 

`sourcename:full/identity/path`  - the full identity of an artifact that is in the artifact source specified by the `sourcename` prefix

`shortname` - the shortened unique name of an artifact that is in the built-in artifact source 

`sourcename:shortname` - the shortened unique name of an artifact that is in the artifact source specified by the `sourcename` prefix

Shortened names are generated based off the shortest unique identity path in the given source.

ie, see the table of full identity to shortnames:

``` bash
# full identity                   short name                       
compilers/microsoft/msvc          msvc
compilers/microsoft/msvc/x64      msvc/x64 # x64 can't be used since there are other packages that end in /x64
compilers/microsoft/msvc/x86      msvc/x86 # x86 can't be used since there are other packages that end in /x86
compilers/microsoft/msvc/arm      msvc/arm # arm can't be used since there are other packages that end in /arm
compilers/microsoft/msvc/arm64    msvc/arm64 # arm64 can't be used since there are other packages that end in /arm64
tools/cmake/x64                   cmake/x64 # there are other '/x64' packages 
tools/cmake/x86                   cmake/x86 # there are other '/x86' packages 
tools/cmake                       cmake     # cmake is unique
tools/cmaker                      cmaker    # cmaker is unique
```

if the artifact is not in the default source, the source name must be specified
``` bash
# full identity                   short name   
mylibs:board/intel/foo            mylibs:foo       # foo is unique, so it's ok
mylibs:board/intel/bar            mylibs:intel/bar # bar is not unique
mylibs:board/intel/biz            mylibs:intel/biz # biz is not unique
mylibs:tools/bar                  mylibs:tools/bar # bar is not unique
mylibs:board/stm32/biz            mylibs:stm32/biz # biz is not unique
mylibs:abclib                     mylibs:abclib    # shortname is the same as the full name in this case
mylibs:something/in/here/xyz      mylibs:xyz       # xyz is unique
```

It is important to note that short names should never be stored in an AMF file, as the short name is only 
valid given current contents of the repository. When an AMF file has a stored identity, it should always be
a full canonical identity.

<hr>

## Versions 
Version numbers are specified using the Semver format.

If a version isn't specified, a range for the latest version (`*`) is assumed

A version or version range can be specified using the [npm semver matching syntax](https://semver.npmjs.com/), 

When a version is stored, it can be stored using the version range specified, a space and then the version found. 
(ie, the first version is what was asked for, the second is what was installed. No need for a separate lock file. )

examples:

``` bash
# Specified         Means
    1.4.1           1.4.1
    1.0.*           the latest version in the 1.0.x range
  * 1.2.3           the user specified the latest *, was given 1.2.3. 
```

<hr>

## Supplying Host/Context information

The host (contextual information regarding the environment+project being installed to) 
should be automatically detected  in some circumstances it may be desired to override 
the detected information and/or specific settings are desired.

Since host/context information can encompass values not anticpated, any 
extra switches on the command line are propogated to the host information block.

Well-known host information can be handled with  

``` bash
# Host archetecture switches
# specifying one of these removes sets the others to false. 
--x64         # overrides arch to identify as x64 ()
--x86         # overrides arch to identify as x86
--arm         # overrides arch to identify as arm (32bit)
--arm64       # overrides arch to identify as arm (64bit)

# Host platform identity switches
# specifying one of these removes sets the others to false. 
--windows     # overrides system to identify as windows
--linux       # overrides system to identify as linux (glibc based?)
--osx         # overrides system to identify as OSX

# other host identity
--project.<property> [value] # [value] defaults to true if not specified

# example project properties
  target   # target CPU arch for compiler target
  desktop  # true when targeting desktop
  one-core # true when targeting onecore
  spectre  # true when using spectre-safe versions of libraries


# 
```

<hr>

<style>
hr.neat {
  height: 1px; 
  border:0;
  background: #333; 
}
h3 {
  border-top:1px solid;
  border-color: #333; 
}
</style>
