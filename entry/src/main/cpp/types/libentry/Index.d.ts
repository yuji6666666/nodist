export const add: (a: number, b: number) => number;
export const nodistVersion: () => string;
export const nodistInstall: (baseDir: string, version: string) => string;
export const nodistList: (baseDir: string) => string;
export const nodistRun: (baseDir: string, version: string, args: string) => string;
