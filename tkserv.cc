#include <fmt/format.h>
#include <fmt/printf.h>
#include <fmt/os.h>
#include <fmt/chrono.h>
#include <fmt/ranges.h>
#include <iostream>
#include <map>
#include "httplib.h"
#include "sqlwriter.hh"
#include "jsonhelper.hh"
#include "support.hh"
#include "pugixml.hpp"
#include "inja.hpp"

using namespace std;
static void replaceSubstring(std::string &originalString, const std::string &searchString, const std::string &replaceString) {
  size_t pos = originalString.find(searchString);
  
  while (pos != std::string::npos) {
    originalString.replace(pos, searchString.length(), replaceString);
    pos = originalString.find(searchString, pos + replaceString.length());
  }
}

static string htmlEscape(const std::string& str)
{
  vector<pair<string,string>> rep{{"&", "&amp;"}, {"<", "&lt;"}, {">", "&gt;"}, {"\"", "&quot;"}, {"'", "&#39;"}};
  string ret=str;
  for(auto&& [from,to] : rep)
    replaceSubstring(ret, from, to);
  return ret;
}

static string getReasonableJPEG(const std::string& id)
{
  if(isPresentNonEmpty(id, "photoscache", ".jpg") && cacheIsNewer(id, "photoscache", ".jpg", "photos")) {
    string fname = makePathForId(id, "photoscache", ".jpg");
    FILE* pfp = fopen(fname.c_str(), "r");
    if(!pfp)
      throw runtime_error("Unable to get cached photo "+id+": "+string(strerror(errno)));
    
    shared_ptr<FILE> fp(pfp, fclose);
    char buffer[4096];
    string ret;
    for(;;) {
      int len = fread(buffer, 1, sizeof(buffer), fp.get());
      if(!len)
	break;
      ret.append(buffer, len);
    }
    if(!ferror(fp.get())) {
      fmt::print("Had a cache hit for {} photo\n", id);
      return ret;
    }
    // otherwise fall back to normal process
  }
  // 
  string fname = makePathForId(id, "photos");
  string command = fmt::format("convert -resize 400 -format jpeg - - < '{}'",
			  fname);
  FILE* pfp = popen(command.c_str(), "r");
  if(!pfp)
    throw runtime_error("Unable to perform conversion for '"+command+"': "+string(strerror(errno)));
  
  shared_ptr<FILE> fp(pfp, pclose);
  char buffer[4096];
  string ret;
  for(;;) {
    int len = fread(buffer, 1, sizeof(buffer), fp.get());
    if(!len)
      break;
    ret.append(buffer, len);
  }
  if(ferror(fp.get()))
    throw runtime_error("Unable to perform pandoc: "+string(strerror(errno)));

  string rsuffix ="."+to_string(getRandom64());
  string oname = makePathForId(id, "photoscache", "", true);
  {
    auto out = fmt::output_file(oname+rsuffix);
    out.print("{}", ret);
  }
  if(rename((oname+rsuffix).c_str(), (oname+".jpg").c_str()) < 0) {
    unlink((oname+rsuffix).c_str());
    fmt::print("Rename of cached JPEG failed\n");
  }
  
  return ret;
}

// for verslag XML, this makes html w/o <html> etc, for use in a .div
static string getHtmlForDocument(const std::string& id)
{
  if(isPresentNonEmpty(id, "doccache", ".html") && cacheIsNewer(id, "doccache", ".html", "docs")) {
    string fname = makePathForId(id, "doccache", ".html");
    FILE* pfp = fopen(fname.c_str(), "r");
    if(!pfp)
      throw runtime_error("Unable to get cached document "+id+": "+string(strerror(errno)));
    
    shared_ptr<FILE> fp(pfp, fclose);
    char buffer[4096];
    string ret;
    for(;;) {
      int len = fread(buffer, 1, sizeof(buffer), fp.get());
      if(!len)
	break;
      ret.append(buffer, len);
    }
    if(!ferror(fp.get())) {
      fmt::print("Had a cache hit for {} HTML\n", id);
      return ret;
    }
    // otherwise fall back to normal process
  }
  
  string fname = makePathForId(id);
  string command;

  if(isDocx(fname))
    command = fmt::format("pandoc -s -f docx   --embed-resources  --variable maxwidth=72em -t html '{}'",
			  fname);
  else if(isRtf(fname))
    command = fmt::format("pandoc -s -f rtf   --embed-resources  --variable maxwidth=72em -t html '{}'",
			  fname);
  else if(isDoc(fname))
    command = fmt::format("echo '<pre>' ; catdoc < '{}'; echo '</pre>'",
			  fname);
  else if(isXML(fname))
    command = fmt::format("xmlstarlet tr tk-div.xslt < '{}'",
			  fname);
  else
    command = fmt::format("pdftohtml -s {} -dataurls -stdout",fname);

  fmt::print("Command: {} {} \n", command, isXML(fname));
  FILE* pfp = popen(command.c_str(), "r");
  if(!pfp)
    throw runtime_error("Unable to perform conversion for '"+command+"': "+string(strerror(errno)));
  
  shared_ptr<FILE> fp(pfp, pclose);
  char buffer[4096];
  string ret;
  for(;;) {
    int len = fread(buffer, 1, sizeof(buffer), fp.get());
    if(!len)
      break;
    ret.append(buffer, len);
  }
  if(ferror(fp.get()))
    throw runtime_error("Unable to perform pdftotext: "+string(strerror(errno)));

  string rsuffix ="."+to_string(getRandom64());
  string oname = makePathForId(id, "doccache", "", true);
  {
    auto out = fmt::output_file(oname+rsuffix);
    out.print("{}", ret);
  }
  if(rename((oname+rsuffix).c_str(), (oname+".html").c_str()) < 0) {
    unlink((oname+rsuffix).c_str());
    fmt::print("Rename of cached HTML failed\n");
  }
  return ret;
}

static string getPDFForDocx(const std::string& id)
{
  if(isPresentNonEmpty(id, "doccache", ".pdf") && cacheIsNewer(id, "doccache", ".pdf", "docs")) {
    string fname = makePathForId(id, "doccache", ".pdf");
    FILE* pfp = fopen(fname.c_str(), "r");
    if(!pfp)
      throw runtime_error("Unable to get cached document "+id+": "+string(strerror(errno)));
    
    shared_ptr<FILE> fp(pfp, fclose);
    char buffer[4096];
    string ret;
    for(;;) {
      int len = fread(buffer, 1, sizeof(buffer), fp.get());
      if(!len)
	break;
      ret.append(buffer, len);
    }
    if(!ferror(fp.get())) {
      fmt::print("Had a cache hit for {} PDF\n", id);
      return ret;
    }
    // otherwise fall back to normal process
  }
  // 
  string fname = makePathForId(id);
  string command = fmt::format("pandoc -s --metadata \"margin-left:1cm\" --metadata \"margin-right:1cm\" -V fontfamily=\"dejavu\"  --variable mainfont=\"DejaVu Serif\" --variable sansfont=Arial --pdf-engine=xelatex -f docx -t pdf '{}'",
			  fname);
  FILE* pfp = popen(command.c_str(), "r");
  if(!pfp)
    throw runtime_error("Unable to perform conversion for '"+command+"': "+string(strerror(errno)));
  
  shared_ptr<FILE> fp(pfp, pclose);
  char buffer[4096];
  string ret;
  for(;;) {
    int len = fread(buffer, 1, sizeof(buffer), fp.get());
    if(!len)
      break;
    ret.append(buffer, len);
  }
  if(ferror(fp.get()))
    throw runtime_error("Unable to perform pandoc: "+string(strerror(errno)));

  string rsuffix ="."+to_string(getRandom64());
  string oname = makePathForId(id, "doccache", "", true);
  {
    auto out = fmt::output_file(oname+rsuffix);
    out.print("{}", ret);
  }
  if(rename((oname+rsuffix).c_str(), (oname+".pdf").c_str()) < 0) {
    unlink((oname+rsuffix).c_str());
    fmt::print("Rename of cached PDF failed\n");
  }
  
  return ret;
}

static string getRawDocument(const std::string& id)
{
  string fname = makePathForId(id);
  FILE* pfp = fopen(fname.c_str(), "r");
  if(!pfp)
    throw runtime_error("Unable to get raw document "+id+": "+string(strerror(errno)));

  shared_ptr<FILE> fp(pfp, fclose);
  char buffer[4096];
  string ret;
  for(;;) {
    int len = fread(buffer, 1, sizeof(buffer), fp.get());
    if(!len)
      break;
    ret.append(buffer, len);
  }
  if(ferror(fp.get()))
    throw runtime_error("Unable to perform pdftotext: "+string(strerror(errno)));
  return ret;
}

struct VoteResult
{
  set<string> voorpartij, tegenpartij, nietdeelgenomenpartij;
  int voorstemmen=0, tegenstemmen=0, nietdeelgenomen=0;
  
};

static string getPartyFromNumber(LockedSqw& sqlw, int nummer)
{
  auto party = sqlw.query("select afkorting from Persoon,fractiezetelpersoon,fractiezetel,fractie where persoon.nummer=? and persoon.functie ='Tweede Kamerlid' and  persoonid=persoon.id and fractiezetel.id=fractiezetelpersoon.fractiezetelid and fractie.id=fractiezetel.fractieid and fractiezetelpersoon.totEnMet=''", {nummer});
  if(party.empty())
    return "";
  return std::get<string>(party[0]["afkorting"]);
}

bool getVoteDetail(LockedSqw& sqlw, const std::string& besluitId, VoteResult& vr)
{
  // er is een mismatch tussen Stemming en Persoon, zie https://github.com/TweedeKamerDerStaten-Generaal/OpenDataPortaal/issues/150
  // dus we gebruiken die tabel maar niet hier
  auto votes = sqlw.query("select * from Stemming where besluitId=?", {besluitId});
  if(votes.empty())
    return false;
  cout<<"Got "<<votes.size()<<" votes for "<<besluitId<<endl;
  bool hoofdelijk = false;
  if(!get<string>(votes[0]["persoonId"]).empty()) {
    fmt::print("Hoofdelijke stemming!\n");
    hoofdelijk=true;
  }
  
  for(auto& v : votes) {
    string soort = get<string>(v["soort"]);
    string partij = get<string>(v["actorFractie"]);
    int zetels = get<int64_t>(v["fractieGrootte"]);
    if(soort == "Voor") {
      if(hoofdelijk) {
	vr.voorstemmen++;
	vr.voorpartij.insert(get<string>(v["actorNaam"]));
      }
      else {
	vr.voorstemmen += zetels;
	vr.voorpartij.insert(partij);
      }
    }
    else if(soort == "Tegen") {
      if(hoofdelijk) {
	vr.tegenstemmen++;
	vr.tegenpartij.insert(get<string>(v["actorNaam"]));
      }
      else {
	vr.tegenstemmen += zetels;
	vr.tegenpartij.insert(partij);
      }
    }
    else if(soort=="Niet deelgenomen") {
      if(hoofdelijk) {
	vr.nietdeelgenomen++;
	vr.nietdeelgenomenpartij.insert(get<string>(v["actorNaam"]));
      }
      else {
	vr.nietdeelgenomen+= zetels;
	vr.nietdeelgenomenpartij.insert(partij);
      }
    }
  }
  return true;
}

time_t getTstamp(const std::string& str)
{
  //  2024-09-17T13:00:00
  //  2024-09-17T13:00:00+0200
  struct tm tm={};
  strptime(str.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
  
  return timelocal(&tm);
}

int main(int argc, char** argv)
{
  SQLiteWriter unlockedsqlw("tk.sqlite3");
  std::mutex sqwlock;
  LockedSqw sqlw{unlockedsqlw, sqwlock};
  signal(SIGPIPE, SIG_IGN); // every TCP application needs this
  httplib::Server svr;

  svr.Get("/getdoc/:nummer", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string nummer=req.path_params.at("nummer"); // 2023D41173
    cout<<"getdoc nummer: "<<nummer<<endl;

    string id, contentType;
    if(nummer.length()==36 && nummer.find_first_not_of("0123456789abcdef-") == string::npos) {
      id = nummer;
      auto ret = sqlw.query("select contentType from Document where id=?", {id});
      if(!ret.empty()) {
	contentType = get<string>(ret[0]["contentType"]);
      }
      else {
	ret = sqlw.query("select contentType from Verslag where id=?", {id});
	if(ret.empty()) {
	  fmt::print("Kon {} niet vinden, niet als document, niet als verslag\n", id);
	  res.set_content("Found nothing!!", "text/plain");
	  res.status=404;
	  return;
	}
	contentType = get<string>(ret[0]["contentType"]);
      }
    }
    else {
      auto ret=sqlw.query("select * from Document where nummer=? order by rowid desc limit 1", {nummer});
      if(ret.empty()) {
	res.set_content("Found nothing!!", "text/plain");
	res.status=404;
	return;
      }
      id = get<string>(ret[0]["id"]);
      contentType = get<string>(ret[0]["contentType"]);
      fmt::print("'{}' {}\n", id, contentType);
    }
    // docx to pdf is better for embedded images it appears
    // XXX disabled
    if(0 && contentType == "application/vnd.openxmlformats-officedocument.wordprocessingml.document") {
      string content = getPDFForDocx(id);
      res.set_content(content, "application/pdf");
    }
    else {
      string content = getHtmlForDocument(id);
      res.set_content(content, "text/html; charset=utf-8");
    }
  });

  // hoe je de fractienaam krijgt bij een persoonId:
  //  select f.afkorting from FractieZetelPersoon fzp,FractieZetel fz,Fractie f, Persoon p where fzp.fractieZetelId = fz.id and fzp.persoonId = p.id and p.id = ?
  
  svr.Get("/getraw/:nummer", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string nummer=req.path_params.at("nummer"); // 2023D41173
    cout<<"getraw nummer: "<<nummer<<endl;
    string id;
    auto ret=sqlw.query("select * from Document where nummer=? order by rowid desc limit 1", {nummer});

    if(ret.empty()) {
      ret = sqlw.query("select * from Verslag where id=? order by rowid desc limit 1", {nummer});
      if(ret.empty()) {
	res.set_content("Found nothing!!", "text/plain");
	return;
      }
      id=nummer;
    }
    else
      id = get<string>(ret[0]["id"]);

    string content = getRawDocument(id);
    res.set_content(content, get<string>(ret[0]["contentType"]));
  });

  svr.Get("/personphoto/:nummer", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string nummer=req.path_params.at("nummer"); // 1234
    cout<<"persoon nummer: "<<nummer<<endl;
    auto ret=sqlw.query("select * from Persoon where nummer=? order by rowid desc limit 1", {nummer});

    if(ret.empty()) {
      res.status = 404;
      res.set_content("No such persoon", "text/plain");
      return;
    }
    string id = get<string>(ret[0]["id"]);
    string content = getReasonableJPEG(id);
    res.set_content(content, "image/jpeg");
  });

  
  svr.Get("/jarig-vandaag", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string f = fmt::format("{:%%-%m-%d}", fmt::localtime(time(0)));
    //auto jarig = sqlw.queryJRet("select geboortedatum,roepnaam,initialen,tussenvoegsel,achternaam,afkorting,persoon.nummer from Persoon,fractiezetelpersoon,fractiezetel,fractie where geboortedatum like ? and persoon.functie ='Tweede Kamerlid' and  persoonid=persoon.id and fractiezetel.id=fractiezetelpersoon.fractiezetelid and fractie.id=fractiezetel.fractieid order by achternaam, roepnaam", {f});
    //res.set_content(jarig.dump(), "application/json");
    return;
  });

  svr.Get("/kamerleden/?", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    auto leden = sqlw.queryJRet("select fractiezetel.gewicht, persoon.*, afkorting from Persoon,fractiezetelpersoon,fractiezetel,fractie where persoon.functie='Tweede Kamerlid' and  persoonid=persoon.id and fractiezetel.id=fractiezetelpersoon.fractiezetelid and fractie.id=fractiezetel.fractieid and totEnMet='' order by afkorting, fractiezetel.gewicht");
    res.set_content(leden.dump(), "application/json");    
  });

  svr.Get("/commissies/?", [&sqlw](const httplib::Request &req, httplib::Response &res) {

    auto j = sqlw.queryJRet("select commissieid,max(datum) mdatum,commissie.afkorting, commissie.naam, inhoudsopgave,commissie.soort from activiteitactor,commissie,activiteit where commissie.id=activiteitactor.commissieid and activiteitactor.activiteitid = activiteit.id group by 1 order by commissie.naam asc"); 

    res.set_content(j.dump(), "application/json");    
  });

  svr.Get("/commissie/:id", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string id = req.path_params.at("id");
    nlohmann::json j = nlohmann::json::object();
    j["leden"] = sqlw.queryJRet("select Commissie.naam, commissie.afkorting cafkorting, commissiezetel.gewicht, CommissieZetelVastPersoon.functie cfunctie, persoon.*, fractie.afkorting fafkorting from Commissie,CommissieZetel,CommissieZetelVastPersoon, Persoon,fractiezetelpersoon,fractiezetel,fractie where Persoon.id=commissiezetelvastpersoon.PersoonId and CommissieZetel.commissieId = Commissie.id and CommissieZetelVastpersoon.CommissieZetelId = commissiezetel.id and fractiezetel.id=fractiezetelpersoon.fractiezetelid and fractie.id=fractiezetel.fractieid and fractiezetelpersoon.persoonId = Persoon.id and commissie.id=? and fractie.datumInactief='' and fractiezetelpersoon.totEnMet='' and CommissieZetelVastPersoon.totEnMet='' order by commissiezetel.gewicht", {id}); 

    j["zaken"] =sqlw.queryJRet("select * from ZaakActor,Zaak where commissieId=? and Zaak.id=ZaakActor.zaakid order by gestartOp desc limit 20", {id});
    j["activiteiten"] =sqlw.queryJRet("select * from ActiviteitActor,Activiteit where commissieId=? and Activiteit.id=ActiviteitActor.activiteitid order by datum desc limit 20", {id});

    auto tmp = sqlw.queryJRet("select activiteit.*, 'Voortouwcommissie' as relatie from activiteit,commissie where voortouwNaam like commissie.naam and commissie.id=? order by datum desc limit 20", {id});
    fmt::print("Got {} voortouw\n", tmp.size());
    for(const auto& t : tmp)
      j["activiteiten"].push_back(t);
		   
    sort(j["activiteiten"].begin(), j["activiteiten"].end(), [](auto& a, auto&b) {
      return a["datum"] > b["datum"];
    });
    res.set_content(j.dump(), "application/json");    
  });

  
  svr.Get("/persoon/:nummer", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    int nummer = atoi(req.path_params.at("nummer").c_str());

    auto lid = sqlw.queryJRet("select persoon.*, afkorting from Persoon,fractiezetelpersoon,fractiezetel,fractie where persoon.nummer=? and  persoonid=persoon.id and fractiezetel.id=fractiezetelpersoon.fractiezetelid and fractie.id=fractiezetel.fractieid and fractiezetelpersoon.totEnMet='' order by achternaam, roepnaam", {nummer});
    if(lid.empty()) {
      res.status=404;
      res.set_content("Geen kamerlid met nummer "+to_string(nummer), "text/plain");
      return;
    }
      
    nlohmann::json j = nlohmann::json::object();
    j["meta"] = lid[0];

    auto zaken = sqlw.queryJRet("select zaak.* from zaakactor,zaak where persoonid=? and relatie='Indiener' and zaak.id=zaakid order by gestartop desc", {(string)lid[0]["id"]});
    for(auto& z: zaken) {
      z["aangenomen"]="";
      z["docs"] = sqlw.queryJRet("select document.* from link,document where link.naar=? and category='Document' and document.id=link.van order by datum", {(string)z["id"]});
      z["besluiten"] = sqlw.queryJRet("select datum,besluit.id,stemmingsoort,tekst from zaak,besluit,agendapunt,activiteit where zaak.nummer=? and besluit.zaakid = zaak.id and agendapunt.id=agendapuntid and activiteit.id=agendapunt.activiteitid order by datum asc", {(string)z["nummer"]});
      for(auto& b : z["besluiten"]) {
	z["aangenomen"]=b["tekst"];
      }
    }
    j["zaken"] = zaken;

    auto verslagen = sqlw.queryJRet("select vergaderingid,datum,soort,zaal,titel from VergaderingSpreker,Persoon,Vergadering where vergadering.id=vergaderingid and Persoon.id=persoonId and persoon.nummer=? and soort != 'Plenair' order by datum desc", {nummer});
    
    j["verslagen"] = verslagen;

    j["activiteiten"] = sqlw.queryJRet("select activiteit.* from ActiviteitActor,activiteit,persoon where persoon.nummer=? and activiteit.id=activiteitid and activiteitactor.persoonid = persoon.id order by datum desc", {nummer});
    res.set_content(j.dump(), "application/json");
    return;
  });

  
  svr.Get("/zaak/:nummer", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string nummer = req.path_params.at("nummer");
    nlohmann::json z = nlohmann::json::object();
    auto zaken = sqlw.query("select * from zaak where nummer=?", {nummer});
    if(zaken.empty()) {
      res.status = 404;
      return;
    }
    z["zaak"] = packResultsJson(zaken)[0];
    string zaakid = z["zaak"]["id"];
    cout<<"Id: '"<<zaakid<<"'\n";
    z["actors"] = sqlw.queryJRet("select * from zaakactor where zaakId=?", {zaakid});

    // Multi: {"activiteit", "agendapunt", "gerelateerdVanuit", "vervangenVanuit"}
    // Hasref: {"activiteit", "agendapunt", "gerelateerdVanuit", "kamerstukdossier", "vervangenVanuit"}


    z["activiteiten"] = sqlw.queryJRet("select * from Activiteit,Link where Link.van=? and Activiteit.id=link.naar", {zaakid});
    z["agendapunten"] = sqlw.queryJRet("select * from Agendapunt,Link where Link.van=? and Agendapunt.id=link.naar", {zaakid});
    for(auto &d : z["agendapunten"]) {
      d["activiteit"] = sqlw.queryJRet("select * from Activiteit where id=?", {(string)d["activiteitId"]})[0];
    }

    z["gerelateerd"] = sqlw.queryJRet("select * from Zaak,Link where Link.van=? and Zaak.id=link.naar and linkSoort='gerelateerdVanuit'", {zaakid});
    z["vervangenVanuit"] = sqlw.queryJRet("select * from Zaak,Link where Link.van=? and Zaak.id=link.naar and linkSoort='vervangenVanuit'", {zaakid});
    z["vervangenDoor"] = sqlw.queryJRet("select * from Zaak,Link where Link.naar=? and Zaak.id=link.van and linkSoort='vervangenVanuit'", {zaakid});
    
    z["docs"] = sqlw.queryJRet("select * from Document,Link where Link.naar=? and Document.id=link.van order by datum desc", {zaakid});
    for(auto &d : z["docs"]) {
      cout<< d["id"] << endl;
      string docid = d["id"];
      d["actors"]=sqlw.queryJRet("select * from DocumentActor where documentId=?", {docid});
    }

    z["kamerstukdossier"]=sqlw.queryJRet("select * from kamerstukdossier where id=?",
					 {(string)z["zaak"]["kamerstukdossierId"]});

    z["besluiten"] = sqlw.queryJRet("select datum,besluit.id,besluit.status, stemmingsoort,tekst from zaak,besluit,agendapunt,activiteit where zaak.nummer=? and besluit.zaakid = zaak.id and agendapunt.id=agendapuntid and activiteit.id=agendapunt.activiteitid order by datum asc", {nummer});

    for(auto& b : z["besluiten"]) {
      VoteResult vr;
      if(getVoteDetail(sqlw, b["id"], vr)) {
	b["voorpartij"] = vr.voorpartij;
	b["tegenpartij"] = vr.tegenpartij;
	b["nietdeelgenomenpartij"] = vr.nietdeelgenomenpartij;
	b["voorstemmen"] = vr.voorstemmen;
	b["tegenstemmen"] = vr.tegenstemmen;
	b["nietdeelgenomenstemmen"] = vr.nietdeelgenomen;
      }
    }
    
    // XXX agendapunt multi
    
    res.set_content(z.dump(), "application/json");
  });    


  svr.Get("/persoonplus/:id", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string id = req.path_params.at("id"); // 9e79de98-e914-4dc8-8dc7-6d7cb09b93d7
    cout<<"Lookup for "<<id<<endl;
    auto persoon = sqlw.query("select * from Persoon where id=?", {id});

    if(persoon.empty()) {
      res.status = 404;
      return;
    }

    nlohmann::json j = nlohmann::json::object();
    j["persoon"] = packResultsJson(persoon)[0];

    j["docs"] = sqlw.queryJRet("select datum,nummer,soort,onderwerp from Document,DocumentActor where relatie like '%ondertekenaar%' and DocumentActor.DocumentId = Document.id and persoonId=? order by 1 desc", {id});

    j["moties"] = sqlw.queryJRet("select document.nummer, besluit.id besluitid, zaak.id zaakid, zaak.nummer zaaknummer, Document.id, Document.datum,document.soort,document.onderwerp,document.titel,besluit.soort,besluit.tekst,besluit.opmerking from Document,DocumentActor,Link,Zaak,besluit where Document.soort like 'Motie%' and DocumentId=Document.id and relatie like '%ondertekenaar' and persoonid=? and link.van = document.id and link.linkSoort='Zaak' and zaak.id=link.naar and besluit.zaakid=zaak.id order by datum desc", {id});
    
    for(auto& m : j["moties"]) {
      VoteResult vr;
      if(!getVoteDetail(sqlw, m["besluitid"], vr))
	continue;
      m["voorpartij"] = vr.voorpartij;
      m["tegenpartij"] = vr.tegenpartij;
      m["voorstemmen"] = vr.voorstemmen;
      m["tegenstemmen"] = vr.tegenstemmen;
      m["nietdeelgenomenstemmen"] = vr.nietdeelgenomen;
      m["aangenomen"] = vr.voorstemmen > vr.tegenstemmen;
    }
    // hier kan een mooie URL van gebakken worden
    //  https://www.tweedekamer.nl/kamerstukken/moties/detail?id=2024Z10238&did=2024D24219
    
    res.set_content(j.dump(), "application/json");
  });

  

  svr.Get("/activiteit/:nummer", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string nummer=req.path_params.at("nummer"); // 2024A02517
    cout<<"/activiteit/:nummer: "<<nummer<<endl;

    auto ret=sqlw.query("select * from Activiteit where nummer=? order by rowid desc limit 1", {nummer});

    
    if(ret.empty()) {
      res.set_content("Found nothing!!", "text/plain");
      return;
    }
    nlohmann::json r = nlohmann::json::object();
    r["meta"] = packResultsJson(ret)[0];
    string activiteitId = r["meta"]["id"];
    auto actors = sqlw.query("select ActiviteitActor.*, Persoon.nummer from ActiviteitActor left join Persoon on Persoon.id=ActiviteitActor.persoonId where activiteitId=? order by volgorde", {activiteitId});
    r["actors"] = packResultsJson(actors);
    auto zalen = sqlw.queryJRet("select * from Reservering,Zaal where Reservering.activiteitId = ? and zaal.id = zaalId", {activiteitId});;
    string zaalnaam;
    if(!zalen.empty()) {
      zaalnaam = zalen[0]["naam"];
      r["zaal"] = zalen[0];
    }
    else
      r["zaal"]="";

    r["agendapunten"]= sqlw.queryJRet("select * from Agendapunt where activiteitId = ? order by volgorde", {activiteitId});

    
    for(auto& ap: r["agendapunten"]) {
      ap["docs"] = sqlw.queryJRet("select * from Document where agendapuntid=?",
				  {(string)ap["id"]});
      ap["zdocs"] = sqlw.queryJRet("select Document.* from link,link link2,zaak,document where link.naar=? and zaak.id=link.van and link2.naar = zaak.id and document.id=link2.van",  {(string)ap["id"]});
    }

    r["docs"] = sqlw.queryJRet("select Document.* from link,Document where linkSoort='Activiteit' and link.naar=? and Document.id=link.van", {activiteitId});

    r["toezeggingen"] = sqlw.queryJRet("select * from Toezegging where activiteitId=?", {activiteitId});
    
    //    string url = fmt::format("https://cdn.debatdirect.tweedekamer.nl/api/agenda/{}",
    //			     ((string)r["meta"]["datum"]).substr(0,10));

    try {
      string url = fmt::format("https://cdn.debatdirect.tweedekamer.nl/search?van={}&tot={}&sortering=relevant&vanaf=0&appVersion=10.34.1&platform=web&totalFormat=new",
			       ((string)r["meta"]["datum"]).substr(0,10),
			       ((string)r["meta"]["datum"]).substr(0,10));
      
      httplib::Client cli("https://cdn.debatdirect.tweedekamer.nl");
      cli.set_connection_timeout(1, 0); 
      cli.set_read_timeout(1, 0); 
      cli.set_write_timeout(1, 0); 
      
      fmt::print("Retrieving from {}\n", url);
      auto httpres = cli.Get(url);
      if(!httpres) {
	auto err = httpres.error();
	fmt::print("Oops retrieving from {} -> {}", url, httplib::to_string(err));
	res.set_content(r.dump(), "application/json");
	return;
      }
      nlohmann::json j = nlohmann::json::parse(httpres->body);
      cout<<j.dump()<<endl;
      //    cout<<httpres->body<<endl;
      string videourl;
      cout<<"onderwerp: "<<(string)(r["meta"]["onderwerp"])<<endl;
      cout<<"datum: "<<(string)(r["meta"]["datum"])<<endl;
      cout<<"tijd: "<<(string)(r["meta"]["aanvangstijd"])<< " - " <<(string)(r["meta"]["eindtijd"])<<endl;
      // tijd: 2024-09-10T16:15:00 - 2024-09-10T16:50:00
      if(zaalnaam.empty())
	zaalnaam = "Plenaire zaal";
      cout<<"zaal: "<<zaalnaam<<endl;
      //    cout <<j["hits"]["hits"].dump()<<endl;
      
      /*
	"startsAt": "2024-09-17T13:00:00+0200",
	"endsAt": "2024-09-17T14:00:00+0200",
	"startedAt": "2024-09-17T13:24:08+0200",
	"endedAt": "2024-09-17T13:43:37+0200",
      */

      time_t tkmidtstamp = (getTstamp((string)(r["meta"]["aanvangstijd"])) + getTstamp((string)(r["meta"]["eindtijd"])))/2;
      time_t tklen = getTstamp((string)(r["meta"]["eindtijd"])) - getTstamp((string)(r["meta"]["aanvangstijd"]));
      
      std::multimap<time_t, nlohmann::json> candidates;
      for(auto& h : j["hits"]["hits"]) {
	auto d = h["_source"];
	time_t ddmidtstamp = (getTstamp((string)d["startsAt"]) + getTstamp((string)d["endsAt"]))/2;
	time_t ddlen = getTstamp((string)d["endsAt"]) - getTstamp((string)d["startsAt"]);
	double lenrat = (ddlen+1.)/(tklen+1.);
	if((string)d["locationName"]==zaalnaam && lenrat >0.1 && lenrat < 10.0)
	  candidates.insert({{abs(ddmidtstamp - tkmidtstamp)}, d});
	fmt::print("'{}' -> '{}' ({}) {} - {} {} {} {}\n",
		   (string)d["name"], (string)d["slug"], (string)d["locationName"],
		   (string)d["startsAt"], (string)d["endsAt"], lenrat,
		   tklen, ddlen
		   );
	
      }
      if(!candidates.empty()) {
	auto c =candidates.begin()->second;
	fmt::print("Best smart match: {} {}\n", (string)c["name"], (string)c["startsAt"]);
	videourl = "https://debatdirect.tweedekamer.nl/" + (string)c["debateDate"] + "/" + (string)c["categoryIds"][0] +"/"+(string)c["locationId"] +"/"+(string)c["slug"];

      }
      r["videourl"]=videourl;
    }
    catch(exception& e) {
      fmt::print("Error getting debatdirect link: {}\n", e.what());
    }
    res.set_content(r.dump(), "application/json");
    
  });


  auto doTemplate = [&](const string& name, const string& file, const string& q = string()) {
    svr.Get("/"+name+"(/?.*)", [&sqlw, name, file, q](const httplib::Request &req, httplib::Response &res) {
      inja::Environment e;
      e.set_html_autoescape(true);
      nlohmann::json data;
      if(!q.empty())
	data["data"] = sqlw.queryJRet(q);
      
      data["pagemeta"]["title"]="";
      data["og"]["title"] = name;
      data["og"]["description"] = name;
      data["og"]["imageurl"] = "";

      res.set_content(e.render_file("./partials/"+file, data), "text/html");
    });
  };

  doTemplate("stemmingen.html", "stemmingen.html");
  doTemplate("kamerstukdossiers.html", "kamerstukdossiers.html");
  doTemplate("vragen.html", "vragen.html");
  doTemplate("commissies.html", "commissies.html");
  doTemplate("verslagen.html", "verslagen.html");
  doTemplate("verslag.html", "verslag.html");
  doTemplate("zaak.html", "zaak.html");
  doTemplate("commissie.html", "commissie.html");
  doTemplate("persoon.html", "persoon.html");
  doTemplate("search.html", "search.html");
  doTemplate("search2.html", "search.html");
  doTemplate("kamerleden.html", "kamerleden.html", "select fractiezetel.gewicht, persoon.*, afkorting from Persoon,fractiezetelpersoon,fractiezetel,fractie where persoon.functie='Tweede Kamerlid' and  persoonid=persoon.id and fractiezetel.id=fractiezetelpersoon.fractiezetelid and fractie.id=fractiezetel.fractieid and totEnMet='' order by afkorting, fractiezetel.gewicht");
  
  doTemplate("geschenken.html", "geschenken.html", "select datum, omschrijving, functie, initialen, tussenvoegsel, roepnaam, achternaam, gewicht,nummer,substr(persoongeschenk.bijgewerkt,0,11)  pgbijgewerkt from persoonGeschenk, Persoon where Persoon.id=persoonId and datum > '2019-01-01' order by persoongeschenk.bijgewerkt desc");


  doTemplate("toezeggingen.html", "toezeggingen.html", "select toezegging.id, tekst, toezegging.nummer, ministerie, status, naamToezegger,activiteit.datum, kamerbriefNakoming, datumNakoming, activiteit.nummer activiteitNummer, initialen, tussenvoegsel, achternaam, functie, fractie.afkorting as fractienaam, voortouwAfkorting from Toezegging,Activiteit left join Persoon on persoon.id = toezegging.persoonId left join Fractie on fractie.id = toezegging.fractieId where  Toezegging.activiteitId = activiteit.id and status != 'Voldaan' order by activiteit.datum desc");

  
  svr.Get("/index.html", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    res.status = 301;
    res.set_header("Location", "./");
  });
  
  svr.Get("/", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    bool onlyRegeringsstukken = req.has_param("onlyRegeringsstukken") && req.get_param_value("onlyRegeringsstukken") != "0";
    string dlim = fmt::format("{:%Y-%m-%d}", fmt::localtime(time(0) - 8*86400));
    nlohmann::json data;
    //auto recentDocs = sqlw.queryJRet("select Document.datum datum, Document.nummer nummer, Document.onderwerp onderwerp, Document.titel titel, Document.soort soort, Document.bijgewerkt bijgewerkt, ZaakActor.naam naam, ZaakActor.afkorting afkorting from Document left join Link on link.van = document.id left join zaak on zaak.id = link.naar left join  ZaakActor on ZaakActor.zaakId = zaak.id and relatie = 'Voortouwcommissie' where bronDocument='' and Document.soort != 'Sprekerslijst' and datum > ? and (? or Document.soort in ('Brief regering', 'Antwoord schriftelijke vragen', 'Voorstel van wet', 'Memorie van toelichting', 'Antwoord schriftelijke vragen (nader)')) order by datum desc, bijgewerkt desc",
//				     {dlim, !onlyRegeringsstukken});

    
    nlohmann::json out = nlohmann::json::array();
    unordered_set<string> seen;
/*    for(auto& rd : recentDocs) {
      if(seen.count(rd["nummer"]))
	continue;
      seen.insert(rd["nummer"]);
      
      if(!rd.count("afkorting"))
	rd["afkorting"]="";
      string datum = ((string)rd["datum"]).substr(0,10);

      rd["datum"]=datum;
      string bijgewerkt = ((string)rd["bijgewerkt"]).substr(0,16);
      replaceSubstring(bijgewerkt, "T", "\xc2\xa0"); // &nsbp;
      replaceSubstring(bijgewerkt, "-", "\xe2\x80\x91"); // Non-Breaking Hyphen[1]
      rd["bijgewerkt"] = bijgewerkt;
      if(((string)rd["titel"]).empty())
	rd["titel"] = rd["soort"];
      out.push_back(rd);
    }*/
    data["recentDocs"] = out;
    
    string f = fmt::format("{:%%-%m-%d}", fmt::localtime(time(0)));
    //data["jarigVandaag"] = sqlw.queryJRet("select geboortedatum,roepnaam,initialen,tussenvoegsel,achternaam,afkorting,persoon.nummer from Persoon,fractiezetelpersoon,fractiezetel,fractie where geboortedatum like ? and persoon.functie ='Tweede Kamerlid' and  persoonid=persoon.id and fractiezetel.id=fractiezetelpersoon.fractiezetelid and fractie.id=fractiezetel.fractieid and fractiezetelpersoon.totEnMet='' order by achternaam, roepnaam", {f});
    
    inja::Environment e;
    e.set_html_autoescape(true);

    data["pagemeta"]["title"]="";
    data["og"]["title"] = "Recente documenten";
    data["og"]["description"] = "Recente documenten uit de Tweede Kamer";
    data["og"]["imageurl"] = "";
    
    res.set_content(e.render_file("./partials/index.html", data), "text/html");
  });

  svr.Get("/recente-kamervragen", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    res.set_content(sqlw.queryJRet("select nummer,onderwerp,naam,gestartOp from Zaak,ZaakActor where zaakid=zaak.id and relatie='Indiener' and gestartOp > '2018-01-01' and soort = 'Schriftelijke vragen' order by gestartOp desc").dump(), "application/json"); // XXX hardcoded date
  });
  
  svr.Get("/open-vragen.html", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    nlohmann::json data;
    auto ovragen =  sqlw.queryJRet("select *, max(persoon.nummer) filter (where relatie ='Indiener') as persoonnummer, max(zaakactor.functie) filter (where relatie='Gericht aan') as aan, max(naam) filter (where relatie='Indiener') as indiener from openvragen,zaakactor,persoon where zaakactor.zaakid = openvragen.id and persoon.id = zaakactor.persoonId group by openvragen.id order by gestartOp desc");

    for(auto& ov : ovragen) {
      ov["gestartOp"] = ((string)ov["gestartOp"]).substr(0,10);
      // ov.aan needs some work
      string aan = ov["aan"];
      replaceSubstring(aan, "minister van", "");
      replaceSubstring(aan, "minister voor", "");
      replaceSubstring(aan, "staatssecretaris van", "");
      ov["aan"] = aan;
      if(ov.count("persoonsnummer"))
	ov["fractie"] = getPartyFromNumber(sqlw, ov["persoonnummer"]);
    }
    data["openVragen"] = ovragen;
    
    inja::Environment e;
    e.set_html_autoescape(true);

    data["pagemeta"]["title"]="";
    data["og"]["title"] = "Open vragen";
    data["og"]["description"] = "Open vragen uit de Tweede Kamer";
    data["og"]["imageurl"] = "";
    
    res.set_content(e.render_file("./partials/open-vragen.html", data), "text/html");
  });


  svr.Get("/besluiten.html", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    nlohmann::json data;
    string dlim = fmt::format("{:%Y-%m-%d}", fmt::localtime(time(0) - 8*86400));
    auto besluiten =  sqlw.queryJRet("select activiteit.datum, activiteit.nummer anummer, zaak.nummer znummer, agendapuntZaakBesluitVolgorde volg, besluit.status,agendapunt.onderwerp aonderwerp, zaak.onderwerp zonderwerp, naam indiener, besluit.tekst from besluit,agendapunt,activiteit,zaak left join zaakactor on zaakactor.zaakid = zaak.id and relatie='Indiener' where besluit.agendapuntid = agendapunt.id and activiteit.id = agendapunt.activiteitid and zaak.id = besluit.zaakid and datum > ? order by datum asc,agendapuntZaakBesluitVolgorde asc", {dlim});

    for(auto& b : besluiten) {
      b["datum"] = ((string)b["datum"]).substr(0,10);
    }
    data["besluiten"] = besluiten;

    cout<<data.dump()<<endl;
    
    inja::Environment e;
    e.set_html_autoescape(true);

    data["pagemeta"]["title"]="";
    data["og"]["title"] = "Recente en toekomstige besluiten";
    data["og"]["description"] = "Recente en toekomstige besluiten in de Tweede Kamer";
    data["og"]["imageurl"] = "";
    
    res.set_content(e.render_file("./partials/besluiten.html", data), "text/html");
  });

  
  // this is still alpine based though somehow!
  svr.Get("/activiteit.html", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string nummer=req.get_param_value("nummer");
    nlohmann::json data;
    auto act = sqlw.queryJRet("select * from Activiteit where nummer=?", {nummer});

    if(act.empty()) {
      res.status=404;
      res.set_content("No such activity", "text/plain");
      return;
    }
    inja::Environment e;
    e.set_html_autoescape(true);

    data["pagemeta"]["title"]="";
    data["og"]["title"] = act[0]["onderwerp"];
    data["og"]["description"] = (string)act[0]["datum"] + ": "+ (string)act[0]["onderwerp"];
    data["og"]["imageurl"] = "";
    
    res.set_content(e.render_file("./partials/activiteit.html", data), "text/html");
  });

  svr.Get("/activiteiten.html", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    // from 4 days ago into the future
    string dlim = fmt::format("{:%Y-%m-%d}", fmt::localtime(time(0)-4*86500));
    
    auto acts = sqlw.queryJRet("select Activiteit.datum datum, activiteit.bijgewerkt bijgewerkt, activiteit.nummer nummer, naam, noot, onderwerp,voortouwAfkorting from Activiteit left join Reservering on reservering.activiteitId=activiteit.id  left join Zaal on zaal.id=reservering.zaalId where datum > ? order by datum asc", {dlim}); 

    for(auto& a : acts) {
      a["naam"] = htmlEscape(a["naam"]);
      a["onderwerp"] = htmlEscape(a["onderwerp"]);
      string datum = a["datum"];
		       
      datum=datum.substr(0,16);
      replaceSubstring(datum, "T", "&nbsp;");
      a["datum"]=datum;
    }
    nlohmann::json data = nlohmann::json::object();
    data["data"] = acts;
    inja::Environment e;
    e.set_html_autoescape(false); // NOTE WELL!

    data["pagemeta"]["title"]="";
    data["og"]["title"] = "Activiteiten";
    data["og"]["description"] = "Activiteiten Tweede Kamer";
    data["og"]["imageurl"] = "";
    
    res.set_content(e.render_file("./partials/activiteiten.html", data), "text/html");
  });

  svr.Get("/ongeplande-activiteiten.html", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    auto acts = sqlw.queryJRet("select * from Activiteit where datum='' order by updated desc"); 

    for(auto& a : acts) {
      a["onderwerp"] = htmlEscape(a["onderwerp"]);
      a["soort"] = htmlEscape(a["soort"]);
      string datum = a["bijgewerkt"];
		       
      datum=datum.substr(0,16);
      replaceSubstring(datum, "T", "&nbsp;");
      a["bijgewerkt"]=datum;
    }
    nlohmann::json data = nlohmann::json::object();
    data["data"] = acts;
    inja::Environment e;
    e.set_html_autoescape(false); // NOTE WELL!

    data["pagemeta"]["title"]="";
    data["og"]["title"] = "Nog ongeplande activiteiten";
    data["og"]["description"] = "Ongeplande activiteiten Tweede Kamer";
    data["og"]["imageurl"] = "";
    
    res.set_content(e.render_file("./partials/ongeplande-activiteiten.html", data), "text/html");
  });


  
  
  svr.Get("/ksd.html", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    int nummer=atoi(req.get_param_value("ksd").c_str()); // 36228
    string toevoeging=req.get_param_value("toevoeging").c_str();
    auto docs = sqlw.queryJRet("select document.nummer docnummer,* from Document,Kamerstukdossier where kamerstukdossier.nummer=? and kamerstukdossier.toevoeging=? and Document.kamerstukdossierid = kamerstukdossier.id order by volgnummer desc", {nummer, toevoeging});
    nlohmann::json data = nlohmann::json::object();
    data["docs"] = docs;

    auto meta = sqlw.query("select * from kamerstukdossier where nummer=? and toevoeging=?",
			   {nummer, toevoeging});

    if(!meta.empty())
      data["meta"] = packResultsJson(meta)[0];
    inja::Environment e;
    e.set_html_autoescape(true);

    data["pagemeta"]["title"]="";
    data["og"]["title"] = docs[0]["titel"];
    data["og"]["description"] = docs[0]["titel"];
    data["og"]["imageurl"] = "";
    
    res.set_content(e.render_file("./partials/ksd.html", data), "text/html");
  });

  
  
  svr.Get("/get/:nummer", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string nummer=req.path_params.at("nummer"); // 2023D41173
    res.status = 301;
    res.set_header("Location", "../document.html?nummer="+nummer);
    res.set_content("Redirecting..", "text/plain");
  });

  svr.Get("/document.html", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string nummer = req.get_param_value("nummer"); // 2023D41173

    nlohmann::json data = nlohmann::json::object();
    auto ret=sqlw.query("select * from Document where nummer=? order by rowid desc limit 1", {nummer});
    if(ret.empty()) {
      res.set_content("Found nothing!!", "text/plain");
      return;
    }

    data["meta"] = packResultsJson(ret)[0];

    data["meta"]["datum"] = ((string)data["meta"]["datum"]).substr(0, 10);

    string bijgewerkt = ((string)data["meta"]["bijgewerkt"]).substr(0, 16);
    bijgewerkt[10]=' ';
    data["meta"]["bijgewerkt"] = bijgewerkt;    
    string kamerstukdossierId, kamerstuktoevoeging;
    int kamerstuknummer=0, kamerstukvolgnummer=0;
    string kamerstuktitel;
    try {
      kamerstukdossierId = get<string>(ret[0]["kamerstukdossierId"]);
      auto kamerstuk = sqlw.query("select * from kamerstukdossier where id=? order by rowid desc limit 1", {kamerstukdossierId});
      if(!kamerstuk.empty()) {
	kamerstuknummer = get<int64_t>(kamerstuk[0]["nummer"]);
	kamerstuktoevoeging = get<string>(kamerstuk[0]["toevoeging"]);
	kamerstuktitel = get<string>(kamerstuk[0]["titel"]);
	kamerstukvolgnummer = get<int64_t>(ret[0]["volgnummer"]);

	data["kamerstuk"]["nummer"]=kamerstuknummer;
	data["kamerstuk"]["toevoeging"]=kamerstuktoevoeging;
	data["kamerstuk"]["titel"]=kamerstuktitel;
	data["kamerstuk"]["volgnummer"]=kamerstukvolgnummer;
      }
    }
    catch(exception& e) {
      fmt::print("Could not get kamerstukdetails: {}\n", e.what());
    }

    string bronDocumentId = std::get<string>(ret[0]["bronDocument"]);
    
    string dir="kamervragen";
    if(data["meta"]["soort"]=="Brief regering")
      dir = "brieven_regering";
    
    data["kamerurl"] = fmt::format("https://www.tweedekamer.nl/kamerstukken/{}/detail?id={}&did={}", 	     dir,
			     get<string>(ret[0]["nummer"]),
			     get<string>(ret[0]["nummer"]));

    

    
    string documentId=get<string>(ret[0]["id"]);
    data["docactors"]= sqlw.queryJRet("select DocumentActor.*, Persoon.nummer from DocumentActor left join Persoon on Persoon.id=Documentactor.persoonId where documentId=? order by relatie", {documentId});

    
    if(!bronDocumentId.empty()) {
      data["brondocumentData"] = sqlw.queryJRet("select * from document where id=? order by rowid desc limit 1", {bronDocumentId});
    }

    data["bijlagen"] = sqlw.queryJRet("select * from document where bronDocument=?", {documentId});
    auto zlinks = sqlw.query("select distinct(naar) as naar, zaak.nummer znummer from Link,Zaak where van=? and naar=zaak.id and category='Document' and linkSoort='Zaak'", {documentId});
    set<string> actids;
    set<string> znummers;
    for(auto& zlink : zlinks) {
      string zaakId = get<string>(zlink["naar"]);
      string znummer = get<string>(zlink["znummer"]);
      auto zactors = sqlw.queryJRet("select * from Zaak,ZaakActor where zaak.id=?  and ZaakActor.zaakId = zaak.id order by relatie", {zaakId});
      data["zaken"][znummer]["actors"] = zactors;

      if(!zactors.empty()) {
	for(auto& z: zactors) 
	  znummers.insert((string)(z["nummer"]));
      }

      data["zaken"][znummer]["docs"] = sqlw.queryJRet("select * from Document,Link where Link.naar=? and link.van=Document.id", {zaakId});
      
      data["zaken"][znummer]["besluiten"] = sqlw.queryJRet("select * from besluit where zaakid=? and verwijderd = 0 order by rowid", {zaakId});
      set<string> agendapuntids;
      for(auto& b: data["zaken"][znummer]["besluiten"]) {
	agendapuntids.insert((string)b["agendapuntId"]);
      }
      
      for(auto& agendapuntid : agendapuntids) {
	auto agendapunten = sqlw.query("select * from Agendapunt where id = ?", {agendapuntid});
	for(auto& agendapunt: agendapunten)
	  actids.insert(get<string>(agendapunt["activiteitId"]));
      }
    }
    data["znummers"]=znummers;
    nlohmann::json activiteiten = nlohmann::json::array();
    
    if(!actids.empty()) {
      for(auto& actid : actids) {
	auto activiteit = sqlw.queryJRet("select * from Activiteit where id = ? order by rowid desc limit 1", {actid});
	for(auto&a : activiteit) {
	  string d = ((string)a["datum"]).substr(0,16);
	  d[10]= ' ';
	  a["datum"] = d;
	  activiteiten.push_back(a);
	}
      }
    }

    // directly linked activity
    auto diract = sqlw.queryJRet("select Activiteit.* from Link,Activiteit where van=? and naar=Activiteit.id", {documentId});
    for(auto&a : diract) {
      string d = ((string)a["datum"]).substr(0,16);
      d[10]= ' ';
      a["datum"] = d;
      activiteiten.push_back(a);
    }
    
    sort(activiteiten.begin(), activiteiten.end(), [](auto&a, auto&b) {
      return a["datum"] < b["datum"];
    });
    data["activiteiten"] = activiteiten;
    string iframe;
    if(get<string>(ret[0]["contentType"])=="application/pdf") {
      string agent;
      if (req.has_header("User-Agent")) {
	agent = req.get_header_value("User-Agent");
      }
      if(agent.find("Firefox") == string::npos && (agent.find("iPhone") != string::npos || agent.find("Android") != string::npos ))
	iframe = "<iframe width='95%'  height='1024' src='../getdoc/"+nummer+"'></iframe>";
      else
	iframe = "<iframe width='95%'  height='1024' src='../getraw/"+nummer+"'></iframe>";
    }
    else
      iframe = "<iframe width='95%'  height='1024' src='../getdoc/"+nummer+"'></iframe>";

    inja::Environment e;
    e.set_html_autoescape(true);

    data["pagemeta"]["title"]=get<string>(ret[0]["onderwerp"]);
    data["og"]["title"] = get<string>(ret[0]["onderwerp"]);
    data["og"]["description"] = get<string>(ret[0]["titel"]) + " " +get<string>(ret[0]["onderwerp"]);
    data["og"]["imageurl"] = "";

    if(get<string>(ret[0]["contentType"])=="application/pdf") {
      string agent;
      if (req.has_header("User-Agent")) {
	agent = req.get_header_value("User-Agent");
      }
      if(agent.find("Firefox") == string::npos && (agent.find("iPhone") != string::npos || agent.find("Android") != string::npos ))
	data["meta"]["iframe"]="getdoc";
      else
	data["meta"]["iframe"]="getraw";
    }
    else
      data["meta"]["iframe"] = "getdoc";
    res.set_content(e.render_file("./partials/getorig.html", data), "text/html");
  });

  
  svr.Get("/vergadering/:vergaderingid", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string id = req.path_params.at("vergaderingid"); // 9e79de98-e914-4dc8-8dc7-6d7cb09b93d7
    auto verslagen = sqlw.query("select * from vergadering,verslag where verslag.vergaderingid=vergadering.id and status != 'Casco' and vergadering.id=? order by datum desc, verslag.updated desc limit 1", {id});
    if(verslagen.empty()) {
      res.status = 404;
      res.set_content("Geen vergadering gevonden", "text/plain");
      return;
    }
    
    // 2024-09-19T12:19:10.3141655Z
    string updated = get<string>(verslagen[0]["updated"]);
    struct tm tm;
    strptime(updated.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
    time_t then = timegm(&tm);
    verslagen[0]["updated"] = fmt::format("{:%Y-%m-%d %H:%M}", fmt::localtime(then));
    // this accidentally gets the "right" id 
    verslagen[0]["htmlverslag"]=getHtmlForDocument(get<string>(verslagen[0]["id"]));
    auto ret = packResultsJson(verslagen);
    res.set_content(ret[0].dump(), "application/json");
  });

  svr.Get("/verslagen", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    
    auto verslagen = sqlw.query("select * from vergadering,verslag where verslag.vergaderingid=vergadering.id and datum > '2023-01-01' and status != 'Casco' order by datum desc, verslag.updated desc");

    set<string> seen;
    decltype(verslagen) tmp;
    for(auto& v: verslagen) {
      string vid = get<string>(v["vergaderingId"]);
      if(seen.count(vid))
	continue;
      tmp.push_back(v);

      seen.insert(vid);
    }
    sort(tmp.begin(), tmp.end(), [](auto&a, auto&b)
    {
      return std::tie(get<string>(a["datum"]), get<string>(a["aanvangstijd"])) >
	std::tie(get<string>(b["datum"]), get<string>(b["aanvangstijd"]));
    });
    res.set_content(packResultsJsonStr(tmp), "application/json");
    fmt::print("Returned {} vergaderverslagen\n", tmp.size());
  });

  
  svr.Get("/open-toezeggingen", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    
    auto docs = sqlw.query("select toezegging.id, tekst, toezegging.nummer, ministerie, status, naamToezegger,activiteit.datum, kamerbriefNakoming, datumNakoming, activiteit.nummer activiteitNummer, initialen, tussenvoegsel, achternaam, functie, fractie.afkorting as fractienaam, voortouwAfkorting from Toezegging,Activiteit left join Persoon on persoon.id = toezegging.persoonId left join Fractie on fractie.id = toezegging.fractieId where  Toezegging.activiteitId = activiteit.id and status != 'Voldaan' order by activiteit.datum desc");
    res.set_content(packResultsJsonStr(docs), "application/json");
    fmt::print("Returned {} open toezeggingen\n", docs.size());
  });


  // create table openvragen as select Zaak.id, Zaak.gestartOp, zaak.nummer, min(document.nummer) as docunummer, zaak.onderwerp, count(1) filter (where Document.soort="Schriftelijke vragen") as numvragen, count(1) filter (where Document.soort like "Antwoord schriftelijke vragen%" or (Document.soort="Mededeling" and (document.onderwerp like '%ingetrokken%' or document.onderwerp like '%intrekken%'))) as numantwoorden  from Zaak, Link, Document where Zaak.id = Link.naar and Document.id = Link.van and Zaak.gestartOp > '2019-09-09' group by 1, 3 having numvragen > 0 and numantwoorden==0 order by 2 desc


  svr.Get("/recent-kamerstukdossiers", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    
    auto docs = sqlw.query("select kamerstukdossier.nummer, max(document.datum) mdatum,kamerstukdossier.titel,kamerstukdossier.toevoeging,hoogsteVolgnummer from kamerstukdossier,document where document.kamerstukdossierid=kamerstukdossier.id and document.datum > '2020-01-01' group by kamerstukdossier.id,toevoeging order by 2 desc");
    // XXX hardcoded date
    res.set_content(packResultsJsonStr(docs), "application/json");
    fmt::print("Returned {} kamerstukdossiers\n", docs.size());
  });

  
  // select * from persoonGeschenk, Persoon where Persoon.id=persoonId order by Datum desc

  svr.Get("/geschenken", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    auto docs = sqlw.query("select datum, omschrijving, functie, initialen, tussenvoegsel, roepnaam, achternaam, gewicht,nummer from persoonGeschenk, Persoon where Persoon.id=persoonId and datum > '2019-01-01' order by Datum desc"); 
    res.set_content(packResultsJsonStr(docs), "application/json");
    fmt::print("Returned {} geschenken\n", docs.size());
  });

  /* stemmingen. Poeh. Een stemming is eigenlijk een Stem, en ze zijn allemaal gekoppeld aan een besluit.
     een besluit heeft een Zaak en een Agendapunt
     een agendapunt hoort weer bij een activiteit, en daar vinden we eindelijk een datum

     Er zijn vaak twee besluiten per motie, eentje "ingediend" en dan nog een besluit waarop
     gestemd wordt.
     
  */

  svr.Get("/stemmingen", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    auto besluiten = sqlw.query("select besluit.id as besluitid, besluit.soort as besluitsoort, besluit.tekst as besluittekst, besluit.opmerking as besluitopmerking, activiteit.datum, activiteit.nummer anummer, zaak.nummer znummer, agendapuntZaakBesluitVolgorde volg, besluit.status,agendapunt.onderwerp aonderwerp, zaak.onderwerp zonderwerp, naam indiener from besluit,agendapunt,activiteit,zaak left join zaakactor on zaakactor.zaakid = zaak.id and relatie='Indiener' where besluit.agendapuntid = agendapunt.id and activiteit.id = agendapunt.activiteitid and zaak.id = besluit.zaakid and datum < '2024-10-20' and datum > '2024-08-13' order by datum desc,agendapuntZaakBesluitVolgorde asc"); // XX hardcoded date

    nlohmann::json j = nlohmann::json::array();

    for(auto& b : besluiten) {
      //      cout<<"Besluit "<<get<string>(b["zonderwerp"])<<endl;
      VoteResult vr;
      if(!getVoteDetail(sqlw, get<string>(b["besluitid"]), vr))
	continue;
      
      fmt::print("{}, voor: {} ({}), tegen: {} ({}), niet deelgenomen: {} ({})\n",
		 get<string>(b["besluitid"]),
		 vr.voorpartij, vr.voorstemmen,
		 vr.tegenpartij, vr.tegenstemmen,
		 vr.nietdeelgenomenpartij, vr.nietdeelgenomen);

      decltype(besluiten) tmp{b};
      nlohmann::json jtmp = packResultsJson(tmp)[0];
      jtmp["voorpartij"] = vr.voorpartij;
      jtmp["tegenpartij"] = vr.tegenpartij;
      jtmp["voorstemmen"] = vr.voorstemmen;
      jtmp["tegenstemmen"] = vr.tegenstemmen;
      jtmp["nietdeelgenomenstemmen"] = vr.nietdeelgenomen;
      j.push_back(jtmp);
    }
    // als document er bij komt kan je deze aantrekkelijke url gebruiken
    // https://www.tweedekamer.nl/kamerstukken/moties/detail?id=2024Z10238&did=2024D24219
    
    res.set_content(j.dump(), "application/json");
    fmt::print("Returned {} besluiten\n", j.size());
  });

  
  svr.Get("/unplanned-activities", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    auto docs = sqlw.query("select * from Activiteit where datum='' order by updated desc"); 
    res.set_content(packResultsJsonStr(docs), "application/json");
    fmt::print("Returned {} unplanned activities\n", docs.size());
  });


  svr.Get("/future-besluiten", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string dlim = fmt::format("{:%Y-%m-%d}", fmt::localtime(time(0) - 8*86400));
    auto docs = sqlw.query("select activiteit.datum, activiteit.nummer anummer, zaak.nummer znummer, agendapuntZaakBesluitVolgorde volg, besluit.status,agendapunt.onderwerp aonderwerp, zaak.onderwerp zonderwerp, naam indiener, besluit.tekst from besluit,agendapunt,activiteit,zaak left join zaakactor on zaakactor.zaakid = zaak.id and relatie='Indiener' where besluit.agendapuntid = agendapunt.id and activiteit.id = agendapunt.activiteitid and zaak.id = besluit.zaakid and datum > ? order by datum asc,agendapuntZaakBesluitVolgorde asc", {dlim}); 
    
    res.set_content(packResultsJsonStr(docs), "application/json");
    fmt::print("Returned {} besluiten\n", docs.size());
  });


  
  svr.Get("/future-activities", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    auto docs = sqlw.query("select Activiteit.datum datum, activiteit.bijgewerkt bijgewerkt, activiteit.nummer nummer, naam, noot, onderwerp,voortouwAfkorting from Activiteit left join Reservering on reservering.activiteitId=activiteit.id  left join Zaal on zaal.id=reservering.zaalId where datum > '2024-09-29' order by datum asc"); // XX hardcoded date

    res.set_content(packResultsJsonStr(docs), "application/json");
    fmt::print("Returned {} activities\n", docs.size());
  });


  svr.Post("/search", [&sqlw](const httplib::Request &req, httplib::Response &res) {
    string term = req.get_file_value("q").content;
    string twomonths = req.get_file_value("twomonths").content;
    string soorten = req.get_file_value("soorten").content;
    string limit = "";
    if(twomonths=="true")
      limit = "2024-08-11";

    // turn COVID-19 into "COVID-19" and A.W.R. Hubert into "A.W.R. Hubert"
    if(term.find_first_of(".-") != string::npos  && term.find('"')==string::npos) {
      cout<<"fixing up"<<endl;
      term = "\"" + term + "\"";
    }
    
    SQLiteWriter idx("tkindex.sqlite3");
    idx.query("ATTACH DATABASE 'tk.sqlite3' as meta");
    idx.query("ATTACH DATABASE ':memory:' as tmp");
    cout<<"Search: '"<<term<<"', limit '"<<limit<<"', soorten: '"<<soorten<<"'"<<endl;
    DTime dt;
    dt.start();
    std::vector<std::unordered_map<std::string,MiniSQLite::outvar_t>> matches; // ugh
    if(soorten=="moties") {
      matches = idx.queryT("SELECT uuid, soort, Document.onderwerp, Document.titel, document.nummer, document.bijgewerkt, document.datum, snippet(docsearch,-1, '<b>', '</b>', '...', 20) as snip, bm25(docsearch) as score, category FROM docsearch, meta.document WHERE docsearch match ? and document.id = uuid and document.datum > ? and document.soort='Motie' order by score limit 280", {term, limit});
    }
    else if(soorten=="vragenantwoorden") {
      matches = idx.queryT("SELECT uuid, soort, Document.onderwerp, Document.titel, document.nummer, document.bijgewerkt, document.datum, snippet(docsearch,-1, '<b>', '</b>', '...', 20) as snip, bm25(docsearch) as score, category FROM docsearch, meta.document WHERE docsearch match ? and document.id = uuid and document.datum > ? and document.soort in ('Schriftelijke vragen', 'Antwoord schriftelijke vragen', 'Antwoord schriftelijke vragen (nader)')  order by score limit 280", {term, limit});
    }
    else {
      // put the matches in a temporary table
      idx.queryT("create table tmp.uuids as SELECT uuid, snippet(docsearch,-1, '<b>', '</b>', '...', 20) as snip, bm25(docsearch) as score, category FROM docsearch WHERE docsearch match ? and datum > ? order by score limit 280", {term, limit});
      
      matches =  idx.queryT("select uuid,meta.Document.onderwerp, meta.Document.bijgewerkt, meta.Document.titel, nummer, datum, snip, score FROM uuids,meta.Document where tmp.uuids.uuid=Document.id");
      
      auto matchesVerslag = idx.queryT("SELECT uuid,meta.Vergadering.titel as onderwerp, meta.Vergadering.id as vergaderingId, meta.Verslag.updated as bijgewerkt, '' as titel, nummer, datum, snip, score FROM uuids, meta.Verslag, meta.Vergadering WHERE uuid = Verslag.id and Vergadering.id = Verslag.vergaderingId");
      
      for(auto& mv : matchesVerslag) {
	fmt::print("Verslag match: {} {} verslagId {}\n", get<string>(mv["onderwerp"]),
		   get<string>(mv["vergaderingId"]),
		   get<string>(mv["uuid"])		 
		 );
	mv["category"]="Vergadering";
	mv["nummer"] =get<string>(mv["vergaderingId"]).substr(0, 8);
	matches.push_back(mv);
      }
    }
    auto usec = dt.lapUsec();
    fmt::print("Got {} matches in {} msec\n", matches.size(), usec/1000.0);
    nlohmann::json response=nlohmann::json::object();
    response["results"]= packResultsJson(matches);

    response["milliseconds"] = usec/1000.0;
    res.set_content(response.dump(), "application/json");
  });

  
  svr.set_exception_handler([](const auto& req, auto& res, std::exception_ptr ep) {
    auto fmt = "<h1>Error 500</h1><p>%s</p>";
    char buf[BUFSIZ];
    try {
      std::rethrow_exception(ep);
    } catch (std::exception &e) {
      snprintf(buf, sizeof(buf), fmt, e.what());
    } catch (...) { // See the following NOTE
      snprintf(buf, sizeof(buf), fmt, "Unknown Exception");
    }
    cout<<"Error: '"<<buf<<"'"<<endl;
    res.set_content(buf, "text/html");
    res.status = 500; 
  });

  svr.set_post_routing_handler([](const auto& req, auto& res) {
    if(endsWith(req.path, ".js") || endsWith(req.path, ".css"))
      res.set_header("Cache-Control", "max-age=3600");
  });
  
  string root = "./html/";
  if(argc > 2)
    root = argv[2];
  svr.set_mount_point("/", root);
  int port = 8089;
  if(argc > 1)
    port = atoi(argv[1]);
  fmt::print("Listening on port {} serving html from {}\n",
	     port, root);
  svr.listen("0.0.0.0", port);
}
