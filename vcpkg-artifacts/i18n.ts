// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/* eslint-disable @typescript-eslint/no-var-requires */
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
      Object.keys(context).forEach(key => sandbox[key] = context[key]);
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

export const safeEval = createSandbox();

type PrimitiveValue = string | number | boolean | undefined | Date;
let currentLocale = require('./locales/messages.json');

export function setLocale(newLocale: string | undefined) {
  if (newLocale) {
    currentLocale = require(newLocale);
  }
}


/**
 * generates the translation key for a given message
 *
 * @param literals
 * @returns the key
 */
function indexOf(literals: TemplateStringsArray) {
  const content = literals.flatMap((k, i) => [k, '$']);
  content.length--; // drop the trailing undefined.
  return content.join('').trim().replace(/ [a-z]/g, ([a, b]) => b.toUpperCase()).replace(/[^a-zA-Z$]/g, '');
}

/**
 * Support for tagged template literals for i18n.
 *
 * Leverages translation files in ../i18n
 *
 * @param literals the literal values in the tagged template
 * @param values the inserted values in the template
 *
 * @translator
 */
export function i(literals: TemplateStringsArray, ...values: Array<string | number | boolean | undefined | Date>): string {
  const key = indexOf(literals);
  if (key) {
    const str = currentLocale[key]; // get localized string
    if (str) {
      // fill out the template string.
      return safeEval(`\`${str}\``, values.reduce((p, c, i) => { p[`p${i}`] = c; return p; }, <any>{}));
    }
  }
  // if the translation isn't available, just resolve the string template normally.
  return String.raw(literals, ...values);
}
