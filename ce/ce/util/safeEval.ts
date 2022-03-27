// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import * as vm from 'vm';

/**
 * Creates a reusable safe-eval sandbox to execute code in.
 */
export function createSandbox(): <T>(code: string, context?: any) => T {
  const sandbox = vm.createContext({});
  return (code: string, context?: any) => {
    const response = 'SAFE_EVAL_' + Math.floor(Math.random() * 1000000);
    sandbox[response] = {};
    if (context) {
      for (const key of Object.keys(context)) {
        sandbox[key] = context[key];
      }
      vm.runInContext(`try {  ${response} = ${code} } catch (e) { ${response} = undefined }`, sandbox);
      for (const key of Object.keys(context)) {
        delete sandbox[key];
      }
    } else {
      vm.runInContext(`${response} = ${code}`, sandbox);
    }
    return sandbox[response];
  };
}
