// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/* eslint-disable prefer-const */

import { lstat } from 'fs/promises';
import { delimiter, extname, resolve } from 'path';
import { isScalar } from 'yaml';
import { undo as undoVariableName } from '../constants';
import { i } from '../i18n';
import { Exports } from '../interfaces/metadata/exports';
import { Session } from '../session';
import { isIterable } from '../util/checks';
import { linq, Record } from '../util/linq';
import { Queue } from '../util/promise';
import { Uri } from '../util/uri';
import { toXml } from '../util/xml';
import { Artifact } from './artifact';

function findCaseInsensitiveOnWindows<V>(map: Map<string, V>, key: string): V | undefined {
  return process.platform === 'win32' ? linq.find(map, key) : map.get(key);
}
export type Tuple<K, V> = [K, V];

export class Activation {

  #defines = new Map<string, string>();
  #aliases = new Map<string, string>();
  #environment = new Map<string, Set<string>>();
  #properties = new Map<string, Set<string>>();

  // Relative to the artifact install
  #locations = new Map<string, string>();
  #paths = new Map<string, Set<string>>();
  #tools = new Map<string, string>();

  #session: Session;
  constructor(session: Session) {
    this.#session = session;
  }

  addExports(exports: Exports, targetFolder: Uri) {
    for (let [define, defineValue] of exports.defines) {
      if (!define) {
        continue;
      }

      if (defineValue === 'true') {
        defineValue = '1';
      }
      this.addDefine(define, defineValue);
    }

    // **** paths ****
    for (const [pathName, values] of exports.paths) {
      if (!pathName || !values || values.length === 0) {
        continue;
      }

      // the folder is relative to the artifact install
      for (const folder of values) {
        this.addPath(pathName, resolve(targetFolder.fsPath, folder));
      }
    }

    // **** tools ****
    for (let [toolName, toolPath] of exports.tools) {
      if (!toolName || !toolPath) {
        continue;
      }
      this.addTool(toolName, resolve(targetFolder.fsPath, toolPath));
    }

    // **** locations ****
    for (const [name, location] of exports.locations) {
      if (!name || !location) {
        continue;
      }

      this.addLocation(name, resolve(targetFolder.fsPath, location));
    }

    // **** variables ****
    for (const [name, environmentVariableValues] of exports.environment) {
      if (!name || environmentVariableValues.length === 0) {
        continue;
      }
      this.addEnvironmentVariable(name, environmentVariableValues);
    }

    // **** properties ****
    for (const [name, propertyValues] of exports.properties) {
      if (!name || propertyValues.length === 0) {
        continue;
      }
      this.addProperty(name, propertyValues);
    }

    // **** aliases ****
    for (const [name, alias] of exports.aliases) {
      if (!name || !alias) {
        continue;
      }
      this.addAlias(name, alias);
    }
  }


  /** a collection of #define declarations that would assumably be applied to all compiler calls. */
  addDefine(name: string, value: string) {
    const v = findCaseInsensitiveOnWindows(this.#defines, name);

    if (v && v !== value) {
      // conflict. todo: what do we want to do?
      this.#session.channels.warning(i`Duplicate define ${name} during activation. New value will replace old.`);
    }
    this.#defines.set(name, value);
  }

  get defines() {
    return linq.entries(this.#defines).selectAsync(async ([key, value]) => <Tuple<string, string>>[key, await this.resolveAndVerify(value)]);
  }

  get definesCount() {
    return this.#defines.size;
  }

  async getDefine(name: string): Promise<string | undefined> {
    const v = this.#defines.get(name);
    return v ? await this.resolveAndVerify(v) : undefined;
  }

  /** a collection of tool locations from artifacts */
  addTool(name: string, value: string) {
    const t = findCaseInsensitiveOnWindows(this.#tools, name);
    if (t && t !== value) {
      this.#session.channels.error(i`Duplicate tool declared ${name} during activation.  New value will replace old.`);
    }
    this.#tools.set(name, value);
  }

  get tools() {
    return linq.entries(this.#tools).selectAsync(async ([key, value]) => <Tuple<string, string>>[key, await this.resolveAndVerify(value)]);
  }

  async getTool(name: string): Promise<string | undefined> {
    const t = findCaseInsensitiveOnWindows(this.#tools, name);
    if (t) {
      const path = await this.resolveAndVerify(t);
      return await this.validatePath(path) ? path : undefined;
    }
    return undefined;
  }

  get toolCount() {
    return this.#tools.size;
  }

  /** Aliases are tools that get exposed to the user as shell aliases */
  addAlias(name: string, value: string) {
    const a = findCaseInsensitiveOnWindows(this.#aliases, name);
    if (a && a !== value) {
      this.#session.channels.error(i`Duplicate alias declared ${name} during activation.  New value will replace old.`);
    }
    this.#aliases.set(name, value);
  }

  async getAlias(name: string, refcheck = new Set<string>()): Promise<string | undefined> {
    const v = findCaseInsensitiveOnWindows(this.#aliases, name);
    if (v !== undefined) {
      return this.resolveAndVerify(v, [], refcheck);
    }
    return undefined;
  }

  get aliases() {
    return linq.entries(this.#aliases).selectAsync(async ([key, value]) => <Tuple<string, string>>[key, await this.resolveAndVerify(value)]);
  }

  get aliasCount() {
    return this.#aliases.size;
  }

  /** a collection of 'published locations' from artifacts. useful for msbuild */
  addLocation(name: string, location: string | Uri) {
    if (!name || !location) {
      return;
    }
    location = typeof location === 'string' ? location : location.fsPath;

    const l = this.#locations.get(name);
    if (l !== location) {
      this.#session.channels.error(i`Duplicate location declared ${name} during activation. New value will replace old.`);
    }
    this.#locations.set(name, location);
  }

  get locations() {
    return linq.entries(this.#locations).selectAsync(async ([key, value]) => <Tuple<string, string>>[key, await this.resolveAndVerify(value)]);
  }

  getLocation(name: string) {
    const l = this.#locations.get(name);
    return l ? this.resolveAndVerify(l) : undefined;
  }
  get locationCount() {
    return this.#locations.size;
  }

  /** a collection of environment variables from artifacts that are intended to be combinined into variables that have PATH delimiters */
  addPath(name: string, location: string | Iterable<string> | Uri | Iterable<Uri>) {
    if (!name || !location) {
      return;
    }

    let set = findCaseInsensitiveOnWindows(this.#paths, name);

    if (!set) {
      set = new Set<string>();
      this.#paths.set(name, set);
    }

    if (isIterable(location)) {
      for (const l of location) {
        set.add(typeof l === 'string' ? l : l.fsPath);
      }
    } else {
      set.add(typeof location === 'string' ? location : location.fsPath);
    }
  }

  get paths() {
    return linq.entries(this.#paths).selectAsync(async ([key, value]) => <Tuple<string, Set<string>>>[key, await this.resolveAndVerify(value)]);
  }

  get pathCount() {
    return this.#paths.size;
  }

  async getPath(name: string) {
    const set = this.#paths.get(name);
    if (!set) {
      return undefined;
    }
    return this.resolveAndVerify(set);
  }

  /** environment variables from artifacts */
  addEnvironmentVariable(name: string, value: string | Iterable<string>) {
    if (!name) {
      return;
    }

    let v = findCaseInsensitiveOnWindows(this.#environment, name);
    if (!v) {
      v = new Set<string>();
      this.#environment.set(name, v);
    }

    if (typeof value === 'string') {
      v.add(value);
    } else {
      for (const each of value) {
        v.add(each);
      }
    }
  }

  get environmentVariables() {
    return linq.entries(this.#environment).selectAsync(async ([key, value]) => <Tuple<string, Set<string>>>[key, await this.resolveAndVerify(value)]);
  }

  get environmentVariableCount() {
    return this.#environment.size;
  }

  /** a collection of arbitrary properties from artifacts. useful for msbuild */
  addProperty(name: string, value: string | Iterable<string>) {
    if (!name) {
      return;
    }
    let v = this.#properties.get(name);
    if (!v) {
      v = new Set<string>();
      this.#properties.set(name, v);
    }

    if (typeof value === 'string') {
      v.add(value);
    } else {
      for (const each of value) {
        v.add(each);
      }
    }
  }

  get properties() {
    return linq.entries(this.#properties).selectAsync(async ([key, value]) => <Tuple<string, Set<string>>>[key, await this.resolveAndVerify(value)]);
  }

  async getProperty(name: string) {
    const v = this.#properties.get(name);
    return v ? await this.resolveAndVerify(v) : undefined;
  }

  get propertyCount() {
    return this.#properties.size;
  }
  async resolveAndVerify(value: string, locals?: Array<string>, refcheck?: Set<string>): Promise<string>
  async resolveAndVerify(value: Set<string>, locals?: Array<string>, refcheck?: Set<string>): Promise<Set<string>>
  async resolveAndVerify(value: string | Set<string>, locals: Array<string> = [], refcheck = new Set<string>()): Promise<string | Set<string>> {
    if (typeof value === 'string') {
      value = this.resolveVariables(value, locals, refcheck);

      if (value.indexOf('{') === -1) {
        return value;
      }
      const parts = value.split(/\{+(.+?)\}+/g);
      const result = [];
      for (let index = 0; index < parts.length; index += 2) {
        result.push(parts[index]);
        result.push(await this.validatePath(parts[index + 1]));
      }
      return result.join('');
    }
    // for sets
    const result = new Set<string>();
    await new Queue().enqueueMany(value, async (v) => result.add(await this.resolveAndVerify(v, locals))).done;
    return result;
  }

  private resolveVariables(text: string, locals: Array<string> = [], refcheck = new Set<string>()): string {
    if (isScalar(text)) {
      this.#session.channels.debug(`internal warning: scalar value being used directly : ${text.value}`);
      text = <any>text.value; // spews a --debug warning if a scalar makes its way thru for some reason
    }

    // short-ciruiting
    if (!text || text.indexOf('$') === -1) {
      return text;
    }

    // prevent circular resolution
    if (refcheck.has(text)) {
      this.#session.channels.warning(i`Circular variable reference detected: ${text}`);
      this.#session.channels.debug(i`Circular variable reference detected: ${text} - ${linq.join(refcheck, ' -> ')}`);
      return text;
    }

    return text.replace(/(\$\$)|(\$)([a-zA-Z_][a-zA-Z0-9_]*)\.([a-zA-Z_][a-zA-Z0-9_]*)|(\$)([a-zA-Z_][a-zA-Z0-9_]*)/g, (wholeMatch, isDoubleDollar, isObjectMember, obj, member, isSimple, variable) => {
      return isDoubleDollar ? '$' : isObjectMember ? this.getValueForVariableSubstitution(obj, member, locals, refcheck) : this.resolveVariables(locals[variable], locals, refcheck);
    });
  }

  private getValueForVariableSubstitution(obj: string, member: string, locals: Array<string>, refcheck: Set<string>): string {
    switch (obj) {
      case 'environment': {
        // lookup environment variable value
        const v = findCaseInsensitiveOnWindows(this.#environment, member);
        if (v) {
          return this.resolveVariables(linq.join(v, ' '), [], refcheck);
        }

        // lookup the environment variable in the original environment
        const orig = this.#session.environment[member];
        if (orig) {
          return orig;
        }
        break;
      }

      case 'defines': {
        const v = findCaseInsensitiveOnWindows(this.#defines, member);
        if (v !== undefined) {
          return this.resolveVariables(v, locals, refcheck);
        }
        break;
      }

      case 'aliases': {
        const v = findCaseInsensitiveOnWindows(this.#aliases, member);
        if (v !== undefined) {
          return this.resolveVariables(v, locals, refcheck);
        }
        break;
      }

      case 'locations': {
        const v = findCaseInsensitiveOnWindows(this.#locations, member);
        if (v !== undefined) {
          return this.resolveVariables(v, locals, refcheck);
        }
        break;
      }

      case 'paths': {
        const v = findCaseInsensitiveOnWindows(this.#paths, member);
        if (v !== undefined) {
          return this.resolveVariables(linq.join(v, delimiter), locals, refcheck);
        }
        break;
      }

      case 'properties': {
        const v = findCaseInsensitiveOnWindows(this.#properties, member);
        if (v !== undefined) {
          return this.resolveVariables(linq.join(v, ';'), locals, refcheck);
        }
        break;
      }

      case 'tools': {
        const v = findCaseInsensitiveOnWindows(this.#tools, member);
        if (v !== undefined) {
          return this.resolveVariables(v, locals, refcheck);
        }
        break;
      }

      default:
        this.#session.channels.warning(i`Variable reference found '$${obj}.${member}' that is referencing an unknown base object.`);
        return `$${obj}.${member}`;
    }

    this.#session.channels.debug(i`Unresolved variable reference found ($${obj}.${member}) during variable substitution.`);
    return `$${obj}.${member}`;
  }


  private async validatePath(path: string) {
    if (path) {
      try {
        if (path[0] === '"') {
          path = path.substr(1, path.length - 2);
        }
        path = resolve(path);
        await lstat(path);

        // if the path has spaces, we need to quote it
        if (path.indexOf(' ') !== -1) {
          path = `"${path}"`;
        }

        return path;
      } catch {
        // does not exist
        this.#session.channels.error(i`Invalid path - does not exist: ${path}`);
      }
    }
    return '';
  }

  expandPathLikeVariableExpressions(value: string): Array<string> {
    let n = undefined;
    const parts = value.split(/(\$[a-zA-Z0-9_.]+)/g).filter(each => each).map((part, i) => {

      const value = this.resolveVariables(part).replace(/\{(.*?)\}/g, (match, expression) => expression);

      if (value.indexOf(delimiter) !== -1) {
        n = i;
      }

      return value;
    });

    if (n === undefined) {
      // if the value didn't have a path separator, then just return the value
      return [parts.join('')];
    }

    const front = parts.slice(0, n).join('');
    const back = parts.slice(n + 1).join('');

    return parts[n].split(delimiter).filter(each => each).map(each => `${front}${each}${back}`);
  }

  async generateMSBuild(artifacts: Iterable<Artifact>): Promise<string> {
    const msbuildFile = {
      Project: {
        $xmlns: 'http://schemas.microsoft.com/developer/msbuild/2003',
        PropertyGroup: <Array<Record<string, any>>>[]
      }
    };

    if (this.locationCount) {
      const locations = <Record<string, any>>{
        $Label: 'Locations'
      };
      for await (const [name, location] of this.locations) {
        locations[name] = location;
      }
      msbuildFile.Project.PropertyGroup.push(locations);
    }

    if (this.propertyCount) {
      const properties = <Record<string, any>>{
        $Label: 'Properties'
      };

      for await (const [name, propertyValues] of this.properties) {
        properties[name] = linq.join(propertyValues, ';');
      }
      msbuildFile.Project.PropertyGroup.push(properties);
    }

    if (this.toolCount) {
      const tools = <Record<string, any>>{
        $Label: 'Tools'
      };

      for await (const [name, tool] of this.tools) {
        tools[name] = tool;
      }
      msbuildFile.Project.PropertyGroup.push(tools);
    }

    if (this.environmentVariableCount) {
      const environment = <Record<string, any>>{
        $Label: 'Environment'
      };

      for await (const [name, envValues] of this.environmentVariables) {
        environment[name] = linq.join(envValues, ';');
      }
      msbuildFile.Project.PropertyGroup.push(environment);
    }

    if (this.pathCount) {
      const paths = <Record<string, any>>{
        $Label: 'Paths'
      };

      for await (const [name, pathValues] of this.paths) {
        paths[name] = linq.join(pathValues, ';');
      }
      msbuildFile.Project.PropertyGroup.push(paths);
    }

    if (this.definesCount) {
      const defines = <Record<string, any>>{
        $Label: 'Defines'
      };

      for await (const [name, define] of this.defines) {
        defines[name] = linq.join(define, ';');
      }
      msbuildFile.Project.PropertyGroup.push(defines);
    }

    if (this.aliasCount) {
      const aliases = <Record<string, any>>{
        $Label: 'Aliases'
      };

      for await (const [name, alias] of this.aliases) {
        aliases[name] = alias;
      }
      msbuildFile.Project.PropertyGroup.push(aliases);
    }

    const propertyGroup = <any>{ $Label: 'Artifacts', Artifacts: { Artifact: [] } };

    for (const artifact of artifacts) {
      propertyGroup.Artifacts.Artifact.push({ $id: artifact.metadata.id, '#text': artifact.targetLocation.fsPath });
    }

    if (propertyGroup.Artifacts.Artifact.length > 0) {
      msbuildFile.Project.PropertyGroup.push(propertyGroup);
    }

    return toXml(msbuildFile);
  }

  protected async generateEnvironmentVariables(originalEnvironment: Record<string, string | undefined>): Promise<[Record<string, string>, Record<string, string>]> {

    const undo = new Record<string, string>();
    const env = new Record<string, string>();

    for await (const [pathVariable, locations] of this.paths) {
      if (locations.size) {
        const originalVariable = linq.find(originalEnvironment, pathVariable) || '';
        if (originalVariable) {
          for (const p of originalVariable.split(delimiter)) {
            if (p) {
              locations.add(p);
            }
          }
        }
        // compose the final value
        env[pathVariable] = linq.join(locations, delimiter);

        // set the undo data
        undo[pathVariable] = originalVariable || '';
      }
    }

    // combine environment variables with multiple values with spaces (uses: CFLAGS, etc)
    for await (const [variable, values] of this.environmentVariables) {
      env[variable] = linq.join(values, ' ');
      undo[variable] = originalEnvironment[variable] || '';
    }

    // .tools get defined as environment variables too.
    for await (const [variable, value] of this.tools) {
      env[variable] = value;
      undo[variable] = originalEnvironment[variable] || '';
    }

    // .defines get compiled into a single environment variable.
    if (this.definesCount) {
      let defines = '';
      for await (const [name, value] of this.defines) {
        defines += value ? `-D${name}=${value} ` : `-D${name} `;
      }
      if (defines) {
        env['DEFINES'] = defines;
        undo['DEFINES'] = originalEnvironment['DEFINES'] || '';
      }
    }

    return [env, undo];
  }

  async activate(artifacts: Iterable<Artifact>, currentEnvironment: Record<string, string | undefined>, shellScriptFile: Uri | undefined, undoEnvironmentFile: Uri | undefined, msbuildFile: Uri | undefined, json: Uri | undefined) {
    let undoDeactivation = '';
    const scriptKind = extname(shellScriptFile?.fsPath || '');

    // load previous activation undo data
    const previous = currentEnvironment[undoVariableName];
    if (previous && undoEnvironmentFile) {
      const deactivationDataFile = this.#session.parseUri(previous);
      if (deactivationDataFile.scheme === 'file' && await deactivationDataFile.exists()) {
        const deactivatationData = JSON.parse(await deactivationDataFile.readUTF8());
        currentEnvironment = undoActivation(currentEnvironment, deactivatationData.environment || {});
        delete currentEnvironment[undoVariableName];
        undoDeactivation = generateScriptContent(scriptKind, deactivatationData.environment || {}, deactivatationData.aliases || {});
      }
    }

    const [variables, undo] = await this.generateEnvironmentVariables(currentEnvironment);

    async function transformtoRecord<T, U = T> (
      orig: AsyncGenerator<Promise<Tuple<string, T>>, any, unknown>, 
      // this type cast to U isn't *technically* correct but since it's locally scoped for this next block of code it shouldn't cause problems
      func: (value: T) => U = (x => x as unknown as U)) {  

      return linq.values((await toArrayAsync(orig))).toObject(tuple => [tuple[0], func(tuple[1])]); 
    }
    
    const defines = await transformtoRecord(this.defines);
    const aliases = await transformtoRecord(this.aliases);
    const locations = await transformtoRecord(this.locations);
    const tools = await transformtoRecord(this.tools);
    const properties = await transformtoRecord(this.properties, (set) => Array.from(set));
    const paths = await transformtoRecord(this.paths, (set) => Array.from(set));

    // generate undo file if requested
    if (undoEnvironmentFile) {
      const undoContents = {
        environment: undo,
        aliases: linq.keys(aliases).select(each => <[string, string]>[each, '']).toObject(each => each)
      };

      // make a note of the location
      variables[undoVariableName] = undoEnvironmentFile.fsPath;

      const contents = JSON.stringify(undoContents, (k, v) => this.#session.serializer(k, v), 2);
      this.#session.channels.verbose(`--------[START UNDO FILE]--------\n${contents}\n--------[END UNDO FILE]---------`);
      // create the file on disk
      await undoEnvironmentFile.writeUTF8(contents);
    }

    // generate shell script if requested
    if (shellScriptFile) {
      const contents = undoDeactivation + generateScriptContent(scriptKind, variables, aliases);

      this.#session.channels.verbose(`--------[START SHELL SCRIPT FILE]--------\n${contents}\n--------[END SHELL SCRIPT FILE]---------`);
      await shellScriptFile.writeUTF8(contents);
    }

    // generate msbuild props file if requested
    if (msbuildFile) {
      const contents = await this.generateMSBuild(artifacts);
      this.#session.channels.verbose(`--------[START MSBUILD FILE]--------\n${contents}\n--------[END MSBUILD FILE]---------`);
      await msbuildFile.writeUTF8(await this.generateMSBuild(artifacts));
    }

    if(json) {
      const contents = generateJson(variables, defines, aliases, properties, locations, paths, tools);
      this.#session.channels.verbose(`--------[START ENV VAR FILE]--------\n${contents}\n--------[END ENV VAR FILE]---------`);
      await json.writeUTF8(contents); 
    }
  }


  /** produces an environment block that can be passed to child processes to leverage dependent artifacts during installtion/activation. */
  async getEnvironmentBlock(): Promise<NodeJS.ProcessEnv> {
    const result = { ... this.#session.environment };

    // add environment variables
    for await (const [k, v] of this.environmentVariables) {
      result[k] = linq.join(v, ' ');
    }

    // update environment path variables
    for await (const [pathVariable, locations] of this.paths) {
      if (locations.size) {
        const originalVariable = linq.find(result, pathVariable) || '';
        if (originalVariable) {
          for (const p of originalVariable.split(delimiter)) {
            if (p) {
              locations.add(p);
            }
          }
        }
        result[pathVariable] = linq.join(locations, delimiter);
      }
    }

    // define tool environment variables
    for await (const [toolName, toolLocation] of this.tools) {
      result[toolName] = toolLocation;
    }

    return result;
  }
}

function generateCmdScript(variables: Record<string, string>, aliases: Record<string, string>): string {
  return linq.entries(variables).select(([k, v]) => { return v ? `set ${k}=${v}` : `set ${k}=`; }).join('\r\n') +
    '\r\n' +
    linq.entries(aliases).select(([k, v]) => { return v ? `doskey ${k}=${v} $*` : `doskey ${k}=`; }).join('\r\n') +
    '\r\n';
}

function generatePowerShellScript(variables: Record<string, string>, aliases: Record<string, string>): string {
  return linq.entries(variables).select(([k, v]) => { return v ? `$\{ENV:${k}}="${v}"` : `$\{ENV:${k}}=$null`; }).join('\n') +
    '\n' +
    linq.entries(aliases).select(([k, v]) => { return v ? `function global:${k} { & ${v} @args }` : `remove-item -ea 0 "function:${k}"`; }).join('\n') +
    '\n';
}

function generatePosixScript(variables: Record<string, string>, aliases: Record<string, string>): string {
  return linq.entries(variables).select(([k, v]) => { return v ? `export ${k}="${v}"` : `unset ${k[0]}`; }).join('\n') +
    '\n' +
    linq.entries(aliases).select(([k, v]) => { return v ? `${k}() {\n  ${v} $* \n}` : `unset -f ${v} > /dev/null 2>&1`; }).join('\n') +
    '\n';
}

function generateScriptContent(kind: string, variables: Record<string, string>, aliases: Record<string, string>) {
  switch (kind) {
    case '.ps1':
      return generatePowerShellScript(variables, aliases);
    case '.cmd':
      return generateCmdScript(variables, aliases);
    case '.sh':
      return generatePosixScript(variables, aliases);
  }
  return '';
}

function generateJson(variables: Record<string, string>, defines: Record<string, string>, aliases: Record<string, string>, 
  properties:Record<string, string[]>, locations: Record<string, string>, paths: Record<string, string[]>, tools: Record<string, string>): string {
    
  var contents = {
    "version": 1, 
    variables,
    defines, 
    aliases, 
    properties, 
    locations, 
    paths, 
    tools
  };
  
  return JSON.stringify(contents);
}

export async function deactivate(shellScriptFile: Uri, variables: Record<string, string>, aliases: Record<string, string>) {
  const kind = extname(shellScriptFile.fsPath);
  await shellScriptFile.writeUTF8(generateScriptContent(kind, variables, aliases));
}

function undoActivation(currentEnvironment: Record<string, string | undefined>, variables: Record<string, string>) {
  const result = { ...currentEnvironment };
  for (const [key, value] of linq.entries(variables)) {
    if (value) {
      result[key] = value;
    } else {
      delete result[key];
    }
  }
  return result;
}

async function toArrayAsync<T>(iterable: AsyncIterable<T>) {
  const result = [];
  for await (const item of iterable) {
    result.push(item);
  }
  return result;
}
