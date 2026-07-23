// place files you want to import through the `$lib` alias in this folder.

import { Format } from './pkg/obot_converter'
import { GameVersion } from './pkg/obot_converter_bg'

export * from './pkg/obot_converter'

export const formats: {
    [key: number]: [string, string, number, boolean]
} = {
    [Format.OmegaBot]: ['OmegaBot 3', 'replay', GameVersion.Version2113, false],
    [Format.OmegaBot2]: ['OmegaBot 2', 'replay', GameVersion.Version2113, true],
    [Format.URL]: ['OmegaBot 1', 'replay', GameVersion.Version2113, true],
    [Format.Tasbot]: ['TASBot', 'json', GameVersion.Version2113, false],
    [Format.MHRBinary]: ['MHR (Binary)', 'mhr', GameVersion.Version2113, false],
    [Format.MHRJson]: ['MHR (Json)', 'mhr.json', GameVersion.Version2113, false],
    [Format.EchoOld]: ['Echo (Old, Json)', 'echo', GameVersion.Version2113, true],
    [Format.EchoNewJson]: ['Echo (New, Json)', 'echo', GameVersion.Version2113, false],
    [Format.EchoNewBinary]: ['Echo (New, Binary)', 'echo', GameVersion.Version2113, false],
    [Format.ZBot]: ['zBot', 'zbf', GameVersion.Version2113, true],
    [Format.KDBot]: ['KD-Bot', 'kd', GameVersion.Version2113, true],
    [Format.Rush]: ['Rush', 'rsh', GameVersion.Version2113, true],
    [Format.ReplayBot]: ['ReplayBot', 'replay', GameVersion.Version2113, true],
    [Format.Fembot]: ['Fembot', 'freplay', GameVersion.Version2113, true],
    [Format.XBot]: ['xBot', 'xbot', GameVersion.Version2113, true],
    [Format.YBot1]: ['yBot 1', '', GameVersion.Version2113, true],
    [Format.YBot]: ['yBot 2', 'ybot', GameVersion.Version2206, false],
    [Format.XDBot]:['XDBot', 'xd', GameVersion.Version2206, false],
    [Format.GDR]: ['GDR', 'gdr', GameVersion.Version2206, false],
    [Format.GDRJson]: ['GDR (Json)', 'gdr.json', GameVersion.Version2206, false],
    [Format.Tcm]: ['TCBot', 'tcm', GameVersion.Version2206, false],
    [Format.Silicate]: ['Silicate v1', 'slc', GameVersion.Version2206, false],
    [Format.Slc2]: ['Silicate v2', 'slc', GameVersion.Version2206, false],
    [Format.Slc3]: ['Silicate v3', 'slc', GameVersion.Version2206, false],
    [Format.PlainText]: ['Plain Text', 'txt', GameVersion.Any, false],
}
