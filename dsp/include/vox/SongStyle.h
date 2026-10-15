#pragma once

#include <string>

/** The song's style (Mixology's list of 75, "genre.style", shared by Voxology, Mixology and Masterology through the
    suite link) and the vocal style Auto-Edit aims for (aeStyle). One song can have rap verses and a melodic hook, so
    the vocal style stays your choice per track: the song's style only suggests it, on a track where you haven't
    picked one yourself. Framework-free, unit-tested (tests/test_songstyle.cpp). */
namespace vox::songstyle
{
    /** aeStyle choices, in order: Trap Lead, Rap, Melodic, Robot, R&B, Pop, Folk, Natural Singer. */
    enum VocalStyle { trapLead = 0, rap, melodic, robot, rnb, pop, folk, naturalSinger };

    /** The vocal style that usually fits a song style ("trap.melodic" -> Melodic); -1 when the key is unknown. */
    inline int vocalStyleFor (const std::string& key)
    {
        const auto dot = key.find ('.');
        if (dot == std::string::npos) return -1;
        const auto genre = key.substr (0, dot), style = key.substr (dot + 1);
        if (genre == "hiphop") return style == "poprap" ? pop : rap;
        if (genre == "trap") return style == "melodic" || style == "plugg" ? melodic : trapLead;
        if (genre == "rnb") return style == "soul" ? naturalSinger : rnb;
        if (genre == "pop")
        {
            if (style == "hyperpop") return robot;
            if (style == "acoustic" || style == "indie") return folk;
            return pop;
        }
        if (genre == "rock") return naturalSinger;
        if (genre == "country") return style == "bluegrass" || style == "americana" ? folk : naturalSinger;
        if (genre == "gospel") return style == "urban" || style == "contemporary" ? rnb : naturalSinger;
        if (genre == "worship") return style == "acoustic" ? folk : naturalSinger;
        if (genre == "electronic") return style == "ambient" ? naturalSinger : pop;
        if (genre == "latin") return style == "reggaeton" ? melodic : style == "pop" || style == "bachata" ? pop : naturalSinger;
        if (genre == "afro") return style == "reggae" ? naturalSinger : melodic;
        if (genre == "jazz") return style == "smooth" ? rnb : naturalSinger;
        if (genre == "folk") return folk;
        if (genre == "cinematic") return naturalSinger;
        return -1;
    }
}
