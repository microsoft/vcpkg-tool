/* eslint-disable keyword-spacing */
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { delimiter, extname } from 'path';
import { undo as undoVariableName } from '../constants';
import { i } from '../i18n';
import { Session } from '../session';
import { Dictionary, linq } from '../util/linq';
import { Uri } from '../util/uri';
import { toXml } from '../util/xml';
import { Artifact } from './artifact';


function generateCmdScript(variables: Dictionary<string>, aliases: Dictionary<string>): string {
  return linq.entries(variables).select(([k, v]) => { return v ? `set ${k}=${v}` : `set ${k}=`; }).join('\r\n') +
    '\r\n' +
    linq.entries(aliases).select(([k, v]) => { return v ? `doskey ${k}=${v} $*` : `doskey ${k}=`; }).join('\r\n') +
    '\r\n';
}

function generatePowerShellScript(variables: Dictionary<string>, aliases: Dictionary<string>): string {
  return linq.entries(variables).select(([k, v]) => { return v ? `$\{ENV:${k}}="${v}"` : `$\{ENV:${k}}=$null`; }).join('\n') +
    '\n' +
    linq.entries(aliases).select(([k, v]) => { return v ? `function global:${k} { ${v} @args }` : `remove-item -ea 0 "function:${k}"`; }).join('\n') +
    '\n';
}

function generatePosixScript(variables: Dictionary<string>, aliases: Dictionary<string>): string {
  return linq.entries(variables).select(([k, v]) => { return v ? `export ${k}="${v}"` : `unset ${k[0]}`; }).join('\n') +
    '\n' +
    linq.entries(aliases).select(([k, v]) => { return v ? `${k}() {\n  ${v} $* \n}` : `unset -f ${v} > /dev/null 2>&1`; }).join('\n') +
    '\n';
}

function generateScriptContent(kind: string, variables: Dictionary<string>, aliases: Dictionary<string>) {
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

export async function deactivate(shellScriptFile: Uri, variables: Dictionary<string>, aliases: Dictionary<string>) {
  const kind = extname(shellScriptFile.fsPath);
  await shellScriptFile.writeUTF8(generateScriptContent(kind, variables, aliases));
}

function undoActivation(currentEnvironment: Dictionary<string | undefined>, variables: Dictionary<string>) {
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


export class Activation {
  #session: Session;
  constructor(session: Session) {
    this.#session = session;
  }

  /** gets a flattend object representation of the activation */
  get output() {
    return {
      defines: Object.fromEntries(this.defines),
      locations: Object.fromEntries([... this.locations.entries()].map(([k, v]) => [k, v.fsPath])),
      properties: Object.fromEntries([... this.properties.entries()].map(([k, v]) => [k, v.join(',')])),
      environment: { ...process.env, ...Object.fromEntries([... this.environment.entries()].map(([k, v]) => [k, linq.join(v, ' ')])) },
      tools: Object.fromEntries(this.tools),
      paths: Object.fromEntries([...this.paths.entries()].map(([k, v]) => [k, linq.values(v).select(each => each.fsPath).join(delimiter)])),
      aliases: Object.fromEntries(this.aliases)
    };
  }

  generateMSBuild(artifacts: Iterable<Artifact>): string {
    const msbuildFile = {
      Project: {
        $xmlns: 'http://schemas.microsoft.com/developer/msbuild/2003',
        PropertyGroup: <Array<Record<string, any>>>[]
      }
    };

    if (this.locations.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Locations', ...linq.entries(this.locations).toObject(([key, value]) => [key, value.fsPath]) });
    }

    if (this.properties.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Properties', ...linq.entries(this.properties).toObject(([key, value]) => [key, value.join(';')]) });
    }

    if (this.tools.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Tools', ...linq.entries(this.tools).toObject(each => each) });
    }

    if (this.environment.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Environment', ...linq.entries(this.environment).toObject(each => each) });
    }

    if (this.paths.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Paths', ...linq.entries(this.paths).toObject(([key, value]) => [key, linq.values(value).select(each => each.fsPath).join(';')]) });
    }

    if (this.defines.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Defines', DEFINES: linq.entries(this.defines).select(([key, value]) => `${key}=${value}`).join(';') });
    }

    if (this.aliases.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Aliases', ...linq.entries(this.environment).toObject(each => each) });
    }

    const propertyGroup = <any>{ $Label: 'Artifacts', Artifacts: { Artifact: [] } };

    for (const artifact of artifacts) {
      propertyGroup.Artifacts.Artifact.push({ $id: artifact.metadata.info.id, '#text': artifact.targetLocation.fsPath });
    }

    if (propertyGroup.Artifacts.Artifact.length > 0) {
      msbuildFile.Project.PropertyGroup.push(propertyGroup);
    }

    return toXml(msbuildFile);
  }

  protected generateEnvironmentVariables(originalEnvironment: Dictionary<string | undefined>): [Dictionary<string>, Dictionary<string>] {

    const undo = new Dictionary<string>();
    const env = new Dictionary<string>();

    for (const [variable, values] of this.paths.entries()) {
      // add new values at the beginning;
      const elements = new Set();
      for (const each of values) {
        elements.add(each.fsPath);
      }

      // add any remaining entries from existing environment
      const originalVariable = originalEnvironment[variable];
      if (originalVariable) {
        for (const p of originalVariable.split(delimiter)) {
          if (p) {
            elements.add(p);
          }
        }

        // compose the final value
        env[variable] = linq.join(elements, delimiter);

        // set the undo data
        undo[variable] = originalVariable || '';
      }

      // combine environment variables with multiple values with spaces (uses: CFLAGS, etc)
      for (const [variable, values] of this.environment) {
        env[variable] = linq.join(values, ' ');
        undo[variable] = originalEnvironment[variable] || '';
      }

      // .tools get defined as environment variables too.
      for (const [variable, value] of this.tools) {
        env[variable] = value;
        undo[variable] = originalEnvironment[variable] || '';
      }

      // .defines get compiled into a single environment variable.
      if (this.defines.size > 0) {
        const defines = linq.entries(this.defines).select(([key, value]) => value !== undefined && value !== '' ? `-D ${key}=${value}` : `-D ${key}`).join(' ');
        if (defines) {
          env['DEFINES'] = defines;
          undo['DEFINES'] = originalEnvironment['DEFINES'] || '';
        }
      }
    }
    return [env, undo];
  }

  async activate(artifacts: Iterable<Artifact>, currentEnvironment: Dictionary<string | undefined>, shellScriptFile: Uri | undefined, undoEnvironmentFile: Uri | undefined, msbuildFile: Uri | undefined) {
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

    const [variables, undo] = this.generateEnvironmentVariables(currentEnvironment);
    const aliases = linq.entries(this.aliases).toObject(each => each);

    // generate undo file if requested
    if (undoEnvironmentFile) {
      const undoContents = {
        environment: undo,
        aliases: linq.keys(aliases).select(each => <[string, string]>[each, '']).toObject(each => each)
      };

      // make a note of the location
      variables[undoVariableName] = undoEnvironmentFile.fsPath;

      // create the file on disk
      await undoEnvironmentFile.writeUTF8(JSON.stringify(undoContents, (k, v) => this.#session.serializer(k, v), 2));
    }

    // generate shell script if requested
    if (shellScriptFile) {
      await shellScriptFile.writeUTF8(undoDeactivation + generateScriptContent(scriptKind, variables, aliases));
    }

    // generate msbuild props file if requested
    if (msbuildFile) {
      await msbuildFile.writeUTF8(this.generateMSBuild(artifacts));
    }
  }

  /** a collection of #define declarations that would assumably be applied to all compiler calls. */
  private defines = new Map<string, string>();
  addDefine(name: string, value: string) {
    const v = process.platform === 'win32' ? linq.find(this.defines, name) : this.defines.get(name);

    if (v && v !== value) {
      // conflict. todo: what do we want to do?
      this.#session.channels.warning(i`Duplicate define ${name} during activation. New value will replace old.`);
    }
    this.defines.set(name, value);
  }

  /** a collection of tool definitions from artifacts (think shell 'aliases')  */
  private tools = new Map<string, string>();
  addTool(name: string, value: string) {
    const t = process.platform === 'win32' ? linq.find(this.tools, name) : this.tools.get(name);
    if (t && t !== value) {
      this.#session.channels.error(i`Duplicate tool declared ${name} during activation.  New value will replace old.`);
    }
    this.tools.set(name, value);
  }

  findTool(name: string): string | undefined {
    return linq.find(this.tools, name);
  }

  /** Aliases are tools that get exposed to the user as shell aliases */
  private aliases = new Map<string, string>();
  addAlias(name: string, value: string) {
    const a = process.platform === 'win32' ? linq.find(this.aliases, name) : this.aliases.get(name);
    if (a && a !== value) {
      this.#session.channels.error(i`Duplicate alias declared ${name} during activation.  New value will replace old.`);
    }
    this.aliases.set(name, value);
  }

  findAlias(name: string): string | undefined {
    return linq.find(this.aliases, name);
  }

  /** a collection of 'published locations' from artifacts. useful for msbuild */
  private locations = new Map<string, Uri>();
  addLocation(name: string, location: Uri) {
    if (!name) {
      return;
    }
    const l = process.platform === 'win32' ? linq.find(this.locations, name) : this.locations.get(name);
    if (l && l.fsPath !== location.fsPath) {
      this.#session.channels.error(i`Duplicate location declared ${name} during activation. New value will replace old.`);
    }
    this.locations.set(name, location);
  }


  /** a collection of environment variables from artifacts that are intended to be combinined into variables that have PATH delimiters */
  private paths = new Map<string, Set<Uri>>();
  addPath(name: string, location: Uri | Array<Uri> | undefined) {
    if (!name || !location) {
      return;
    }

    let set = process.platform === 'win32' ? linq.find(this.paths, name) : this.paths.get(name);
    if (!set) {
      set = new Set<Uri>();
      this.paths.set(name, set);
    }
    if (Array.isArray(location)) {
      for (const l of location) {
        set.add(l);
      }
    } else {
      set.add(location);
    }
  }

  /** environment variables from artifacts */
  private environment = new Map<string, Set<string>>();
  addEnvironmentVariable(name: string, value: string | Iterable<string>) {
    if (!name) {
      return;
    }

    let v = process.platform === 'win32' ? linq.find(this.environment, name) : this.environment.get(name);
    if (!v) {
      v = new Set<string>();
      this.environment.set(name, v);
    }

    if (typeof value === 'string') {
      v.add(value);
    } else {
      for (const each of value) {
        v.add(each);
      }
    }
  }

  /** a collection of arbitrary properties from artifacts. useful for msbuild */
  private properties = new Map<string, Array<string>>();
  addProperty(name: string, value: string | Iterable<string>) {
    if (!name) {
      return;
    }

    if (typeof value === 'string') {
      this.properties.getOrDefault(name, []).push(value);
    } else {
      this.properties.getOrDefault(name, []).push(...value);
    }
  }

  /** produces an environment block that can be passed to child processes to leverage dependent artifacts during installtion/activation. */
  get environmentBlock(): NodeJS.ProcessEnv {
    const result = { ... this.#session.environment };

    // add environment variables
    for (const [k, v] of linq.entries(this.environment)) {
      result[k] = linq.join(v, ' ');
    }


    // update environment paths
    for (const [pathVariable, items] of this.paths) {
      if (items.size) {
        const s = new Set(...linq.values(items).select(each => each.fsPath));
        const originalVariable = result[pathVariable] || '';
        if (originalVariable) {
          for (const p of originalVariable.split(delimiter)) {
            if (p) {
              s.add(p);
            }
          }
        }
        result[pathVariable] = linq.join(s, delimiter);
      }
    }

    // define tool environment variables
    for (const [key, value] of this.tools) {
      result[key] = value;
    }

    return result;
  }
}