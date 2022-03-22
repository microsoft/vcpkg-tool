# Build Instructions

## Prerequisites: 
1. [Node.js](https://nodejs.org/en/download/) (>=15.10.0) -- We suggest using [NVS](https://github.com/jasongin/nvs)

2. [Rush](https://rushjs.io/pages/intro/welcome/) 

## VSCode extensions:
  - `ESLint`
  - `Mocha Test Explorer`
  - `Test Explorer UI`


``` bash
npm install -g @microsoft/rush
```

## Preparation
The first time (or after pulling from upstream) use Rush to install modules packages.
``` bash
rush update 
```

## Building code
- `rush rebuild` to build the modules
or
- `rush watch` to setup the build watcher 
or
- in VSCode, just build (ctrl-shift-b)

## Important Rush commands

### `rush update` 
Ensures that all the dependencies are installed for all the projects.  
Safe to run mulitple times. 
If you modify a `package.json` file you should re-run this. (or merge from upstream when `package.json` is updated)

### `rush purge` 
Cleans out all the installed dependencies.  
If you run this, you must run `rush update` before building.

### `rush rebuild` 
Rebuilds all the projects from scratch

### `rush watch` 
Runs the typescript compiler in `--watch` mode to watch for modified files on disk.

### `rush test` 
Runs `npm test` on each project (does not build first -- see `rush test-ci`)

### `rush test-ci`
Runs `npm test-ci` on each project (rebuilds the code then runs the tests)

### `rush clean`  
Runs `npm clean` on each project -- cleans out the `./dist` folder for each project.  
Needs to have packages installed, so it can only be run after `rush update`

### `rush lint` 
Runs `npm lint` on each project (runs the eslint on the source)

### `rush fix`
Runs `npm eslint-fix` on each project (fixes all fixable linter errors)

### `rush set-versions` 
This will set the `x.x.build` verison for each project based on the commit number. 

### `rush sync-versions`
This will ensure that all the projects have consistent versions for all dependencies, and ensures that cross-project version references are set correctly




# Building a release

To create the final npm tgz file we're going to statically include our dependencies so that we're not having to pull them down dynamically.

using `rush deploy` will generate this in `./common/deploy` which will copy all the necessary runtime components, and the contents of the `./assets/` folder (which contains the `package.json` and scripts to build/install the cli correctly. )

``` bash
npx rush update                 # install the packages for the project
npx rush rebuild                # build the typescript files
npx rush deploy --overwrite     # create the common/deploy folder with all the contents includeing ./assets
npm pack ./common/deploy        # pack it up
```