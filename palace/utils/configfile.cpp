// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "configfile.hpp"

#include <algorithm>
#include <mfem.hpp>
#include <nlohmann/json.hpp>

namespace palace::config
{

using json = nlohmann::json;

namespace
{

template <std::size_t N>
SymmetricMatrixData<N> ParseSymmetricMatrixData(json &mat, std::string name,
                                                SymmetricMatrixData<N> data)
{
  auto it = mat.find(name);
  if (it != mat.end() && it->is_array())
  {
    // Attempt to parse as an array.
    data.s = it->get<std::array<double, N>>();
  }
  else
  {
    // Fall back to scalar parsing with default.
    double s = mat.value(name, data.s[0]);
    data.s.fill(s);
  }
  data.v = mat.value("MaterialAxes", data.v);
  return data;
}

void CheckDirection(std::string &str, bool port)
{
  std::transform(str.begin(), str.end(), str.begin(), ::tolower);
  if (port)
  {
    MFEM_VERIFY((str.size() == 1 &&
                 (str[0] == 'x' || str[0] == 'y' || str[0] == 'z' || str[0] == 'r')) ||
                    (str.size() == 2 && (str[0] == '+' || str[0] == '-') &&
                     (str[1] == 'x' || str[1] == 'y' || str[1] == 'z' || str[1] == 'r')),
                "Invalid format for boundary direction or side!");
  }
  else
  {
    MFEM_VERIFY((str.size() == 1 && (str[0] == 'x' || str[0] == 'y' || str[0] == 'z')) ||
                    (str.size() == 2 && (str[0] == '+' || str[0] == '-') &&
                     (str[1] == 'x' || str[1] == 'y' || str[1] == 'z')),
                "Invalid format for boundary direction or side!");
  }
  if (str.length() == 1)
  {
    str.insert(0, "+");
  }
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &data)
{
  bool first = true;
  for (const auto &x : data)
  {
    os << (first ? x : (' ' << x));
    first = false;
  }
  return os;
}

template <typename T, std::size_t N>
std::ostream &operator<<(std::ostream &os, const std::array<T, N> &data)
{
  bool first = true;
  for (const auto &x : data)
  {
    os << (first ? x : (' ' << x));
    first = false;
  }
  return os;
}

template <std::size_t N>
std::ostream &operator<<(std::ostream &os, const SymmetricMatrixData<N> &data)
{
  os << "s: " << data.s;
  int j = 0;
  for (const auto &x : data.v)
  {
    os << ", v" << j++ << ": " << x;
  }
  return os;
}

}  // namespace

// Helper for converting string keys to enum for ProblemData::Type.
NLOHMANN_JSON_SERIALIZE_ENUM(ProblemData::Type,
                             {{ProblemData::Type::INVALID, nullptr},
                              {ProblemData::Type::DRIVEN, "Driven"},
                              {ProblemData::Type::EIGENMODE, "Eigenmode"},
                              {ProblemData::Type::ELECTROSTATIC, "Electrostatic"},
                              {ProblemData::Type::MAGNETOSTATIC, "Magnetostatic"},
                              {ProblemData::Type::TRANSIENT, "Transient"}})

void ProblemData::SetUp(json &config)
{
  auto problem = config.find("Problem");
  MFEM_VERIFY(problem != config.end(),
              "\"Problem\" must be specified in configuration file!");
  MFEM_VERIFY(problem->find("Type") != problem->end(),
              "Missing config[\"Problem\"][\"Type\"] in configuration file!");
  type = problem->at("Type");  // Required
  MFEM_VERIFY(type != ProblemData::Type::INVALID,
              "Invalid value for config[\"Problem\"][\"Type\"] in configuration file!");
  verbose = problem->value("Verbose", verbose);
  output = problem->value("Output", output);

  // Check for provided solver configuration data (not required for electrostatics or
  // magnetostatics since defaults can be used for every option).
  auto solver = config.find("Solver");
  if (type == ProblemData::Type::DRIVEN)
  {
    MFEM_VERIFY(solver->find("Driven") != solver->end(),
                "config[\"Problem\"][\"Type\"] == \"Driven\" should be accompanied by a "
                "config[\"Solver\"][\"Driven\"] configuration!");
  }
  else if (type == ProblemData::Type::EIGENMODE)
  {
    MFEM_VERIFY(solver->find("Eigenmode") != solver->end(),
                "config[\"Problem\"][\"Type\"] == \"Eigenmode\" should be accompanied by a "
                "config[\"Solver\"][\"Eigenmode\"] configuration!");
  }
  else if (type == ProblemData::Type::ELECTROSTATIC)
  {
    // MFEM_VERIFY(
    //     solver->find("Electrostatic") != solver->end(),
    //     "config[\"Problem\"][\"Type\"] == \"Electrostatic\" should be accompanied by a "
    //     "config[\"Solver\"][\"Electrostatic\"] configuration!");
  }
  else if (type == ProblemData::Type::MAGNETOSTATIC)
  {
    // MFEM_VERIFY(
    //     solver->find("Magnetostatic") != solver->end(),
    //     "config[\"Problem\"][\"Type\"] == \"Magnetostatic\" should be accompanied by a "
    //     "config[\"Solver\"][\"Magnetostatic\"] configuration!");
  }
  else if (type == ProblemData::Type::TRANSIENT)
  {
    MFEM_VERIFY(solver->find("Transient") != solver->end(),
                "config[\"Problem\"][\"Type\"] == \"Transient\" should be accompanied by a "
                "config[\"Solver\"][\"Transient\"] configuration!");
  }

  // Cleanup
  problem->erase("Type");
  problem->erase("Verbose");
  problem->erase("Output");
  MFEM_VERIFY(problem->empty(),
              "Found an unsupported configuration file keyword under \"Problem\"!\n"
                  << problem->dump(2));

  // Debug
  // std::cout << "Type: " << type << '\n';
  // std::cout << "Verbose: " << verbose << '\n';
  // std::cout << "Output: " << output << '\n';
}

void RefinementData::SetUp(json &model)
{
  auto refinement = model.find("Refinement");
  if (refinement == model.end())
  {
    return;
  }
  uniform_ref_levels = refinement->value("UniformLevels", uniform_ref_levels);
  MFEM_VERIFY(uniform_ref_levels >= 0,
              "Number of uniform mesh refinement levels must be non-negative!");
  auto boxes = refinement->find("Boxes");
  if (boxes != refinement->end())
  {
    MFEM_VERIFY(boxes->is_array(), "config[\"Refinement\"][\"Boxes\"] should specify an "
                                   "array in the configuration file!");
    for (auto it = boxes->begin(); it != boxes->end(); ++it)
    {
      auto xlim = it->find("XLimits");
      auto ylim = it->find("YLimits");
      auto zlim = it->find("ZLimits");
      MFEM_VERIFY(
          xlim != it->end() && ylim != it->end() && zlim != it->end(),
          "Missing \"Boxes\" refinement region \"X/Y/ZLimits\" in configuration file!");
      MFEM_VERIFY(xlim->is_array() && ylim->is_array() && zlim->is_array(),
                  "config[\"Refinement\"][\"Boxes\"][\"X/Y/ZLimits\"] should specify an "
                  "array in the "
                  "configuration file!");
      MFEM_VERIFY(it->find("Levels") != it->end(),
                  "Missing \"Boxes\" refinement region \"Levels\" in configuration file!");
      boxlist.emplace_back();
      BoxRefinementData &data = boxlist.back();
      data.ref_levels = it->at("Levels");  // Required

      std::vector<double> bx = xlim->get<std::vector<double>>();  // Required
      MFEM_VERIFY(bx.size() == 2,
                  "config[\"Refinement\"][\"Boxes\"][\"XLimits\"] should specify an "
                  "array of length 2 in the configuration file!");
      if (bx[1] < bx[0])
      {
        std::swap(bx[0], bx[1]);
      }
      data.bbmin.push_back(bx[0]);
      data.bbmax.push_back(bx[1]);

      std::vector<double> by = ylim->get<std::vector<double>>();  // Required
      MFEM_VERIFY(by.size() == 2,
                  "config[\"Refinement\"][\"Boxes\"][\"YLimits\"] should specify an "
                  "array of length 2 in the configuration file!");
      if (by[1] < by[0])
      {
        std::swap(by[0], by[1]);
      }
      data.bbmin.push_back(by[0]);
      data.bbmax.push_back(by[1]);

      std::vector<double> bz = zlim->get<std::vector<double>>();  // Required
      MFEM_VERIFY(bz.size() == 2,
                  "config[\"Refinement\"][\"Boxes\"][\"ZLimits\"] should specify an "
                  "array of length 2 in the configuration file!");
      if (bz[1] < bz[0])
      {
        std::swap(bz[0], bz[1]);
      }
      data.bbmin.push_back(bz[0]);
      data.bbmax.push_back(bz[1]);

      // Cleanup
      it->erase("Levels");
      it->erase("XLimits");
      it->erase("YLimits");
      it->erase("ZLimits");
      MFEM_VERIFY(it->empty(), "Found an unsupported configuration file keyword under "
                               "config[\"Refinement\"][\"Boxes\"]!\n"
                                   << it->dump(2));

      // Debug
      // std::cout << "Levels: " << data.ref_levels << '\n';
      // std::cout << "BoxMin: " << data.bbmin << '\n';
      // std::cout << "BoxMax: " << data.bbmax << '\n';
    }
  }
  auto spheres = refinement->find("Spheres");
  if (spheres != refinement->end())
  {
    MFEM_VERIFY(spheres->is_array(), "config[\"Refinement\"][\"Spheres\"] should specify "
                                     "an array in the configuration file!");
    for (auto it = spheres->begin(); it != spheres->end(); ++it)
    {
      auto ctr = it->find("Center");
      MFEM_VERIFY(ctr != it->end() && it->find("Radius") != it->end(),
                  "Missing \"Spheres\" refinement region \"Center\" or \"Radius\" in "
                  "configuration file!");
      MFEM_VERIFY(ctr->is_array(),
                  "config[\"Refinement\"][\"Spheres\"][\"Center\"] should specify "
                  "an array in the configuration file!");
      MFEM_VERIFY(
          it->find("Levels") != it->end(),
          "Missing \"Spheres\" refinement region \"Levels\" in configuration file!");
      spherelist.emplace_back();
      SphereRefinementData &data = spherelist.back();
      data.ref_levels = it->at("Levels");             // Required
      data.r = it->at("Radius");                      // Required
      data.center = ctr->get<std::vector<double>>();  // Required
      MFEM_VERIFY(data.center.size() == 3, "config[\"Refinement\"][\"Spheres\"][\"Center\"]"
                                           " should specify an array of length "
                                           "3 in the configuration file!");

      // Cleanup
      it->erase("Levels");
      it->erase("Radius");
      it->erase("Center");
      MFEM_VERIFY(it->empty(), "Found an unsupported configuration file keyword under "
                               "config[\"Refinement\"][\"Spheres\"]!\n"
                                   << it->dump(2));

      // Debug
      // std::cout << "Levels: " << data.ref_levels << '\n';
      // std::cout << "Radius: " << data.r << '\n';
      // std::cout << "Center: " << data.center << '\n';
    }
  }

  // Cleanup
  refinement->erase("UniformLevels");
  refinement->erase("Boxes");
  refinement->erase("Spheres");
  MFEM_VERIFY(refinement->empty(),
              "Found an unsupported configuration file keyword under \"Refinement\"!\n"
                  << refinement->dump(2));

  // Debug
  // std::cout << "UniformLevels: " << uniform_ref_levels << '\n';
}

void ModelData::SetUp(json &config)
{
  auto model = config.find("Model");
  MFEM_VERIFY(model != config.end(), "\"Model\" must be specified in configuration file!");
  MFEM_VERIFY(model->find("Mesh") != model->end(),
              "Missing config[\"Model\"][\"Mesh\"] file in configuration file!");
  mesh = model->at("Mesh");  // Required
  L0 = model->value("L0", L0);
  Lc = model->value("Lc", Lc);
  partition = model->value("Partition", partition);
  reorient_tet = model->value("ReorientTetMesh", reorient_tet);
  refinement.SetUp(*model);

  // Cleanup
  model->erase("Mesh");
  model->erase("L0");
  model->erase("Lc");
  model->erase("Partition");
  model->erase("ReorientTetMesh");
  model->erase("Refinement");
  MFEM_VERIFY(model->empty(),
              "Found an unsupported configuration file keyword under \"Model\"!\n"
                  << model->dump(2));

  // Debug
  // std::cout << "Mesh: " << mesh << '\n';
  // std::cout << "L0: " << L0 << '\n';
  // std::cout << "Lc: " << Lc << '\n';
  // std::cout << "Partition: " << partition << '\n';
  // std::cout << "ReorientTetMesh: " << reorient_tet << '\n';
}

void MaterialDomainData::SetUp(json &domains)
{
  auto materials = domains.find("Materials");
  MFEM_VERIFY(materials != domains.end() && materials->is_array(),
              "\"Materials\" must be specified as an array in configuration file!");
  for (auto it = materials->begin(); it != materials->end(); ++it)
  {
    MFEM_VERIFY(
        it->find("Attributes") != it->end(),
        "Missing \"Attributes\" list for \"Materials\" domain in configuration file!");
    vecdata.emplace_back();
    MaterialData &data = vecdata.back();
    data.attributes = it->at("Attributes").get<std::vector<int>>();  // Required
    data.mu_r = ParseSymmetricMatrixData(*it, "Permeability", data.mu_r);
    data.epsilon_r = ParseSymmetricMatrixData(*it, "Permittivity", data.epsilon_r);
    data.tandelta = ParseSymmetricMatrixData(*it, "LossTan", data.tandelta);
    data.sigma = ParseSymmetricMatrixData(*it, "Conductivity", data.sigma);
    data.lambda_L = it->value("LondonDepth", data.lambda_L);

    // Debug
    // std::cout << "Attributes: " << data.attributes << '\n';
    // std::cout << "Permeability: " << data.mu_r << '\n';
    // std::cout << "Permittivity: " << data.epsilon_r << '\n';
    // std::cout << "LossTan: " << data.tandelta << '\n';
    // std::cout << "Conductivity: " << data.sigma << '\n';
    // std::cout << "LondonDepth: " << data.lambda_L << '\n';

    // Cleanup
    it->erase("Attributes");
    it->erase("Permeability");
    it->erase("Permittivity");
    it->erase("LossTan");
    it->erase("Conductivity");
    it->erase("MaterialAxes");
    it->erase("LondonDepth");
    MFEM_VERIFY(it->empty(),
                "Found an unsupported configuration file keyword under \"Materials\"!\n"
                    << it->dump(2));
  }
}

void DomainDielectricPostData::SetUp(json &postpro)
{
  auto dielectric = postpro.find("Dielectric");
  if (dielectric == postpro.end())
  {
    return;
  }
  MFEM_VERIFY(dielectric->is_array(),
              "\"Dielectric\" should specify an array in the configuration file!");
  for (auto it = dielectric->begin(); it != dielectric->end(); ++it)
  {
    MFEM_VERIFY(it->find("Index") != it->end(),
                "Missing \"Dielectric\" domain \"Index\" in configuration file!");
    MFEM_VERIFY(
        it->find("Attributes") != it->end(),
        "Missing \"Attributes\" list for \"Dielectric\" domain in configuration file!");
    auto ret = mapdata.insert(std::make_pair(it->at("Index"), DomainDielectricData()));
    MFEM_VERIFY(ret.second, "Repeated \"Index\" found when processing \"Dielectric\" "
                            "domains in configuration file!");
    DomainDielectricData &data = ret.first->second;
    data.attributes = it->at("Attributes").get<std::vector<int>>();  // Required

    // Debug
    // std::cout << "Index: " << ret.first->first << '\n';
    // std::cout << "Attributes: " << data.attributes << '\n';

    // Cleanup
    it->erase("Index");
    it->erase("Attributes");
    MFEM_VERIFY(it->empty(),
                "Found an unsupported configuration file keyword under \"Dielectric\"!\n"
                    << it->dump(2));
  }
}

void ProbePostData::SetUp(json &postpro)
{
  auto probe = postpro.find("Probe");
  if (probe == postpro.end())
  {
    return;
  }
  MFEM_VERIFY(probe->is_array(),
              "\"Probe\" should specify an array in the configuration file!");
  for (auto it = probe->begin(); it != probe->end(); ++it)
  {
    MFEM_VERIFY(it->find("Index") != it->end(),
                "Missing \"Probe\" point \"Index\" in configuration file!");
    MFEM_VERIFY(it->find("X") != it->end() && it->find("Y") != it->end() &&
                    it->find("Z") != it->end(),
                "Missing \"Probe\" point \"X\", \"Y\", or \"Z\" in configuration file!");
    auto ret = mapdata.insert(std::make_pair(it->at("Index"), ProbeData()));
    MFEM_VERIFY(
        ret.second,
        "Repeated \"Index\" found when processing \"Probe\" points in configuration file!");
    ProbeData &data = ret.first->second;
    data.x = it->at("X");  // Required
    data.y = it->at("Y");  // Required
    data.z = it->at("Z");  // Required

    // Debug
    // std::cout << "Index: " << ret.first->first << '\n';
    // std::cout << "X: " << data.x << '\n';
    // std::cout << "Y: " << data.y << '\n';
    // std::cout << "Z: " << data.z << '\n';

    // Cleanup
    it->erase("Index");
    it->erase("X");
    it->erase("Y");
    it->erase("Z");
    MFEM_VERIFY(it->empty(),
                "Found an unsupported configuration file keyword under \"Probe\"!\n"
                    << it->dump(2));
  }
}

void DomainPostData::SetUp(json &domains)
{
  auto postpro = domains.find("Postprocessing");
  if (postpro == domains.end())
  {
    return;
  }
  dielectric.SetUp(*postpro);
  probe.SetUp(*postpro);

  // Store all unique postprocessing domain attributes.
  for (const auto &[idx, data] : dielectric)
  {
    attributes.insert(data.attributes.begin(), data.attributes.end());
  }

  // Cleanup
  postpro->erase("Dielectric");
  postpro->erase("Probe");
  MFEM_VERIFY(postpro->empty(),
              "Found an unsupported configuration file keyword under \"Postprocessing\"!\n"
                  << postpro->dump(2));
}

void DomainData::SetUp(json &config)
{
  auto domains = config.find("Domains");
  MFEM_VERIFY(domains != config.end(),
              "\"Domains\" must be specified in configuration file!");
  materials.SetUp(*domains);
  postpro.SetUp(*domains);

  // Store all unique domain attributes.
  for (const auto &data : materials)
  {
    attributes.insert(data.attributes.begin(), data.attributes.end());
  }
  for (const auto &attr : postpro.attributes)
  {
    MFEM_VERIFY(attributes.find(attr) != attributes.end(),
                "Domain postprocessing can only be enabled on domains which have a "
                "corresponding \"Materials\" entry!");
  }

  // Cleanup
  domains->erase("Materials");
  domains->erase("Postprocessing");
  MFEM_VERIFY(domains->empty(),
              "Found an unsupported configuration file keyword under \"Domains\"!\n"
                  << domains->dump(2));
}

void PecBoundaryData::SetUp(json &boundaries)
{
  auto pec = boundaries.find("PEC");
  auto ground = boundaries.find("Ground");
  if (pec == boundaries.end() && ground == boundaries.end())
  {
    return;
  }
  if (pec == boundaries.end())
  {
    pec = ground;
  }
  else if (ground == boundaries.end())  // Do nothing
  {
  }
  else
  {
    MFEM_ABORT(
        "Configuration file should not specify both \"PEC\" and \"Ground\" boundaries!");
  }
  MFEM_VERIFY(pec->find("Attributes") != pec->end(),
              "Missing \"Attributes\" list for \"PEC\" boundary in configuration file!");
  attributes = pec->at("Attributes").get<std::vector<int>>();  // Required
  std::sort(attributes.begin(), attributes.end());

  // Cleanup
  pec->erase("Attributes");
  MFEM_VERIFY(pec->empty(),
              "Found an unsupported configuration file keyword under \"PEC\"!\n"
                  << pec->dump(2));

  // Debug
  // std::cout << "PEC:";
  // for (auto attr : attributes)
  // {
  //   std::cout << ' ' << attr;
  // }
  // std::cout << '\n';
}

void PmcBoundaryData::SetUp(json &boundaries)
{
  auto pmc = boundaries.find("PMC");
  auto zeroq = boundaries.find("ZeroCharge");
  if (pmc == boundaries.end() && zeroq == boundaries.end())
  {
    return;
  }
  if (pmc == boundaries.end())
  {
    pmc = zeroq;
  }
  else if (zeroq == boundaries.end())  // Do nothing
  {
  }
  else
  {
    MFEM_ABORT("Configuration file should not specify both \"PMC\" and \"ZeroCharge\" "
               "boundaries!");
  }
  MFEM_VERIFY(pmc->find("Attributes") != pmc->end(),
              "Missing \"Attributes\" list for \"PMC\" boundary in configuration file!");
  attributes = pmc->at("Attributes").get<std::vector<int>>();  // Required
  std::sort(attributes.begin(), attributes.end());

  // Cleanup
  pmc->erase("Attributes");
  MFEM_VERIFY(pmc->empty(),
              "Found an unsupported configuration file keyword under \"PMC\"!\n"
                  << pmc->dump(2));

  // Debug
  // std::cout << "PMC:";
  // for (auto attr : attributes)
  // {
  //   std::cout << ' ' << attr;
  // }
  // std::cout << '\n';
}

void WavePortPecBoundaryData::SetUp(json &boundaries)
{
  auto pec = boundaries.find("WavePortPEC");
  if (pec == boundaries.end())
  {
    return;
  }
  MFEM_VERIFY(
      pec->find("Attributes") != pec->end(),
      "Missing \"Attributes\" list for \"WavePortPEC\" boundary in configuration file!");
  attributes = pec->at("Attributes").get<std::vector<int>>();  // Required
  std::sort(attributes.begin(), attributes.end());

  // Cleanup
  pec->erase("Attributes");
  MFEM_VERIFY(pec->empty(),
              "Found an unsupported configuration file keyword under \"WavePortPEC\"!\n"
                  << pec->dump(2));

  // Debug
  // std::cout << "WavePortPEC:";
  // for (auto attr : attributes)
  // {
  //   std::cout << ' ' << attr;
  // }
  // std::cout << '\n';
}

void FarfieldBoundaryData::SetUp(json &boundaries)
{
  auto absorbing = boundaries.find("Absorbing");
  if (absorbing == boundaries.end())
  {
    return;
  }
  MFEM_VERIFY(
      absorbing->find("Attributes") != absorbing->end(),
      "Missing \"Attributes\" list for \"Absorbing\" boundary in configuration file!");
  attributes = absorbing->at("Attributes").get<std::vector<int>>();  // Required
  std::sort(attributes.begin(), attributes.end());
  order = absorbing->value("Order", order);
  MFEM_VERIFY(order == 1 || order == 2,
              "Supported values for config[\"Absorbing\"][\"Order\"] are 1 or 2!");

  // Cleanup
  absorbing->erase("Attributes");
  absorbing->erase("Order");
  MFEM_VERIFY(absorbing->empty(),
              "Found an unsupported configuration file keyword under \"Absorbing\"!\n"
                  << absorbing->dump(2));

  // Debug
  // std::cout << "Absorbing:";
  // for (auto attr : attributes)
  // {
  //   std::cout << ' ' << attr;
  // }
  // std::cout << '\n';
  // std::cout << "Order: " << order << '\n';
}

void ConductivityBoundaryData::SetUp(json &boundaries)
{
  auto conductivity = boundaries.find("Conductivity");
  if (conductivity == boundaries.end())
  {
    return;
  }
  MFEM_VERIFY(conductivity->is_array(),
              "\"Conductivity\" should specify an array in the configuration file!");
  for (auto it = conductivity->begin(); it != conductivity->end(); ++it)
  {
    MFEM_VERIFY(
        it->find("Attributes") != it->end(),
        "Missing \"Attributes\" list for \"Conductivity\" boundary in configuration file!");
    MFEM_VERIFY(
        it->find("Conductivity") != it->end(),
        "Missing \"Conductivity\" boundary \"Conductivity\" in configuration file!");
    vecdata.emplace_back();
    ConductivityData &data = vecdata.back();
    data.attributes = it->at("Attributes").get<std::vector<int>>();  // Required
    data.sigma = it->at("Conductivity");                             // Required
    data.mu_r = it->value("Permeability", data.mu_r);
    data.h = it->value("Thickness", data.h);
    data.external = it->value("External", data.external);

    // Debug
    // std::cout << "Attributes: " << data.attributes << '\n';
    // std::cout << "Conductivity: " << data.sigma << '\n';
    // std::cout << "Permeability: " << data.mu_r << '\n';
    // std::cout << "Thickness: " << data.h << '\n';
    // std::cout << "External: " << data.external << '\n';

    // Cleanup
    it->erase("Attributes");
    it->erase("Conductivity");
    it->erase("Permeability");
    it->erase("Thickness");
    it->erase("External");
    MFEM_VERIFY(it->empty(),
                "Found an unsupported configuration file keyword under \"Conductivity\"!\n"
                    << it->dump(2));
  }
}

void ImpedanceBoundaryData::SetUp(json &boundaries)
{
  auto impedance = boundaries.find("Impedance");
  if (impedance == boundaries.end())
  {
    return;
  }
  MFEM_VERIFY(impedance->is_array(),
              "\"Impedance\" should specify an array in the configuration file!");
  for (auto it = impedance->begin(); it != impedance->end(); ++it)
  {
    MFEM_VERIFY(
        it->find("Attributes") != it->end(),
        "Missing \"Attributes\" list for \"Impedance\" boundary in configuration file!");
    vecdata.emplace_back();
    ImpedanceData &data = vecdata.back();
    data.attributes = it->at("Attributes").get<std::vector<int>>();  // Required
    data.Rs = it->value("Rs", data.Rs);
    data.Ls = it->value("Ls", data.Ls);
    data.Cs = it->value("Cs", data.Cs);

    // Debug
    // std::cout << "Attributes: " << data.attributes << '\n';
    // std::cout << "Rs: " << data.Rs << '\n';
    // std::cout << "Ls: " << data.Ls << '\n';
    // std::cout << "Cs: " << data.Cs << '\n';

    // Cleanup
    it->erase("Attributes");
    it->erase("Rs");
    it->erase("Ls");
    it->erase("Cs");
    MFEM_VERIFY(it->empty(),
                "Found an unsupported configuration file keyword under \"Impedance\"!\n"
                    << it->dump(2));
  }
}

void LumpedPortBoundaryData::SetUp(json &boundaries)
{
  auto port = boundaries.find("LumpedPort");
  auto terminal = boundaries.find("Terminal");
  if (port == boundaries.end() && terminal == boundaries.end())
  {
    return;
  }
  if (port == boundaries.end())
  {
    port = terminal;
  }
  else if (terminal == boundaries.end())  // Do nothing
  {
  }
  else
  {
    MFEM_ABORT("Configuration file should not specify both \"LumpedPort\" and \"Terminal\" "
               "boundaries!");
  }
  MFEM_VERIFY(
      port->is_array(),
      "\"LumpedPort\" and \"Terminal\" should specify an array in the configuration file!");
  for (auto it = port->begin(); it != port->end(); ++it)
  {
    MFEM_VERIFY(
        it->find("Index") != it->end(),
        "Missing \"LumpedPort\" or \"Terminal\" boundary \"Index\" in configuration file!");
    auto ret = mapdata.insert(std::make_pair(it->at("Index"), LumpedPortData()));
    MFEM_VERIFY(ret.second, "Repeated \"Index\" found when processing \"LumpedPort\" or "
                            "\"Terminal\" boundaries in configuration file!");
    LumpedPortData &data = ret.first->second;
    data.R = it->value("R", data.R);
    data.L = it->value("L", data.L);
    data.C = it->value("C", data.C);
    data.Rs = it->value("Rs", data.Rs);
    data.Ls = it->value("Ls", data.Ls);
    data.Cs = it->value("Cs", data.Cs);
    data.excitation = it->value("Excitation", data.excitation);
    if (it->find("Attributes") != it->end())
    {
      MFEM_VERIFY(it->find("Elements") == it->end(),
                  "Cannot specify both top-level \"Attributes\" list and \"Elements\" for "
                  "\"LumpedPort\" or \"Terminal\" boundary in configuration file!");
      data.nodes.resize(1);
      LumpedPortData::Node &node = data.nodes.back();
      node.attributes = it->at("Attributes").get<std::vector<int>>();  // Required
      node.direction = it->value("Direction", node.direction);
      if (terminal == boundaries.end())
      {
        // Only check direction for true ports, not terminals.
        CheckDirection(node.direction, true);
      }
    }
    else
    {
      auto elements = it->find("Elements");
      MFEM_VERIFY(elements != it->end(),
                  "Missing top-level \"Attributes\" list or \"Elements\" for "
                  "\"LumpedPort\" or \"Terminal\" boundary in configuration file!");
      for (auto elem_it = elements->begin(); elem_it != elements->end(); ++elem_it)
      {
        MFEM_VERIFY(elem_it->find("Attributes") != elem_it->end(),
                    "Missing \"Attributes\" list for \"LumpedPort\" or \"Terminal\" "
                    "boundary element in configuration file!");
        data.nodes.emplace_back();
        LumpedPortData::Node &node = data.nodes.back();
        node.attributes = elem_it->at("Attributes").get<std::vector<int>>();  // Required
        node.direction = elem_it->value("Direction", node.direction);
        if (terminal == boundaries.end())
        {
          // Only check direction for true ports, not terminals.
          CheckDirection(node.direction, true);
        }

        // Cleanup
        elem_it->erase("Attributes");
        elem_it->erase("Direction");
        MFEM_VERIFY(elem_it->empty(),
                    "Found an unsupported configuration file keyword under \"LumpedPort\" "
                    "or \"Terminal\" boundary element!\n"
                        << elem_it->dump(2));
      }
    }

    // Debug
    // std::cout << "Index: " << ret.first->first << '\n';
    // std::cout << "R: " << data.R << '\n';
    // std::cout << "L: " << data.L << '\n';
    // std::cout << "C: " << data.C << '\n';
    // std::cout << "Rs: " << data.Rs << '\n';
    // std::cout << "Ls: " << data.Ls << '\n';
    // std::cout << "Cs: " << data.Cs << '\n';
    // std::cout << "Excitation: " << data.excitation << '\n';
    // for (const auto &node : data.nodes)
    // {
    //   std::cout << "Attributes: " << node.attributes << '\n';
    //   std::cout << "Direction: " << node.direction << '\n';
    // }

    // Cleanup
    it->erase("Index");
    it->erase("R");
    it->erase("L");
    it->erase("C");
    it->erase("Rs");
    it->erase("Ls");
    it->erase("Cs");
    it->erase("Excitation");
    it->erase("Attributes");
    it->erase("Direction");
    it->erase("Elements");
    MFEM_VERIFY(it->empty(), "Found an unsupported configuration file keyword under "
                             "\"LumpedPort\" or \"Terminal\"!\n"
                                 << it->dump(2));
  }
}

void WavePortBoundaryData::SetUp(json &boundaries)
{
  auto port = boundaries.find("WavePort");
  if (port == boundaries.end())
  {
    return;
  }
  MFEM_VERIFY(port->is_array(),
              "\"WavePort\" should specify an array in the configuration file!");
  for (auto it = port->begin(); it != port->end(); ++it)
  {
    MFEM_VERIFY(it->find("Index") != it->end(),
                "Missing \"WavePort\" boundary \"Index\" in configuration file!");
    MFEM_VERIFY(
        it->find("Attributes") != it->end(),
        "Missing \"Attributes\" list for \"WavePort\" boundary in configuration file!");
    auto ret = mapdata.insert(std::make_pair(it->at("Index"), WavePortData()));
    MFEM_VERIFY(ret.second, "Repeated \"Index\" found when processing \"WavePort\" "
                            "boundaries in configuration file!");
    WavePortData &data = ret.first->second;
    data.attributes = it->at("Attributes").get<std::vector<int>>();  // Required
    data.mode_idx = it->value("Mode", data.mode_idx);
    MFEM_VERIFY(data.mode_idx > 0,
                "\"WavePort\" boundary \"Mode\" must be positive (1-based)!");
    data.d_offset = it->value("Offset", data.d_offset);
    data.excitation = it->value("Excitation", data.excitation);

    // Debug
    // std::cout << "Index: " << ret.first->first << '\n';
    // std::cout << "Attributes: " << data.attributes << '\n';
    // std::cout << "Mode: " << data.mode_idx << '\n';
    // std::cout << "Offset: " << data.d_offset << '\n';
    // std::cout << "Excitation: " << data.excitation << '\n';

    // Cleanup
    it->erase("Index");
    it->erase("Attributes");
    it->erase("Mode");
    it->erase("Offset");
    it->erase("Excitation");
    MFEM_VERIFY(it->empty(),
                "Found an unsupported configuration file keyword under \"WavePort\"!\n"
                    << it->dump(2));
  }
}

void SurfaceCurrentBoundaryData::SetUp(json &boundaries)
{
  auto source = boundaries.find("SurfaceCurrent");
  if (source == boundaries.end())
  {
    return;
  }
  MFEM_VERIFY(source->is_array(),
              "\"SurfaceCurrent\" should specify an array in the configuration file!");
  for (auto it = source->begin(); it != source->end(); ++it)
  {
    MFEM_VERIFY(it->find("Index") != it->end(),
                "Missing \"SurfaceCurrent\" source \"Index\" in configuration file!");
    auto ret = mapdata.insert(std::make_pair(it->at("Index"), SurfaceCurrentData()));
    MFEM_VERIFY(ret.second, "Repeated \"Index\" found when processing \"SurfaceCurrent\" "
                            "boundaries in configuration file!");
    SurfaceCurrentData &data = ret.first->second;
    if (it->find("Attributes") != it->end())
    {
      MFEM_VERIFY(it->find("Elements") == it->end(),
                  "Cannot specify both top-level \"Attributes\" list and \"Elements\" for "
                  "\"SurfaceCurrent\" boundary in configuration file!");
      data.nodes.resize(1);
      SurfaceCurrentData::Node &node = data.nodes.back();
      node.attributes = it->at("Attributes").get<std::vector<int>>();  // Required
      node.direction = it->value("Direction", node.direction);
      CheckDirection(node.direction, true);
    }
    else
    {
      auto elements = it->find("Elements");
      MFEM_VERIFY(
          elements != it->end(),
          "Missing top-level \"Attributes\" list or \"Elements\" for \"SurfaceCurrent\" "
          "boundary in configuration file!");
      for (auto elem_it = elements->begin(); elem_it != elements->end(); ++elem_it)
      {
        MFEM_VERIFY(
            elem_it->find("Attributes") != elem_it->end(),
            "Missing \"Attributes\" list for \"SurfaceCurrent\" boundary element in "
            "configuration file!");
        data.nodes.emplace_back();
        SurfaceCurrentData::Node &node = data.nodes.back();
        node.attributes = it->at("Attributes").get<std::vector<int>>();  // Required
        node.direction = it->value("Direction", node.direction);
        CheckDirection(node.direction, true);

        // Cleanup
        elem_it->erase("Attributes");
        elem_it->erase("Direction");
        MFEM_VERIFY(elem_it->empty(), "Found an unsupported configuration file keyword "
                                      "under \"SurfaceCurrent\" boundary element!\n"
                                          << elem_it->dump(2));
      }
    }

    // Debug
    // std::cout << "Index: " << ret.first->first << '\n';
    // for (const auto &node : data.nodes)
    // {
    //   std::cout << "Attributes: " << node.attributes << '\n';
    //   std::cout << "Direction: " << node.direction << '\n';
    // }

    // Cleanup
    it->erase("Index");
    it->erase("Attributes");
    it->erase("Direction");
    it->erase("Elements");
    MFEM_VERIFY(
        it->empty(),
        "Found an unsupported configuration file keyword under \"SurfaceCurrent\"!\n"
            << it->dump(2));
  }
}

void CapacitancePostData::SetUp(json &postpro)
{
  auto capacitance = postpro.find("Capacitance");
  if (capacitance == postpro.end())
  {
    return;
  }
  MFEM_VERIFY(capacitance->is_array(),
              "\"Capacitance\" should specify an array in the configuration file!");
  for (auto it = capacitance->begin(); it != capacitance->end(); ++it)
  {
    MFEM_VERIFY(it->find("Index") != it->end(),
                "Missing \"Capacitance\" boundary \"Index\" in configuration file!");
    MFEM_VERIFY(
        it->find("Attributes") != it->end(),
        "Missing \"Attributes\" list for \"Capacitance\" boundary in configuration file!");
    auto ret = mapdata.insert(std::make_pair(it->at("Index"), CapacitanceData()));
    MFEM_VERIFY(ret.second, "Repeated \"Index\" found when processing \"Capacitance\" "
                            "boundaries in configuration file!");
    CapacitanceData &data = ret.first->second;
    data.attributes = it->at("Attributes").get<std::vector<int>>();  // Required

    // Debug
    // std::cout << "Index: " << ret.first->first << '\n';
    // std::cout << "Attributes: " << data.attributes << '\n';

    // Cleanup
    it->erase("Index");
    it->erase("Attributes");
    MFEM_VERIFY(it->empty(),
                "Found an unsupported configuration file keyword under \"Capacitance\"!\n"
                    << it->dump(2));
  }
}

void InductancePostData::SetUp(json &postpro)
{
  auto inductance = postpro.find("Inductance");
  if (inductance == postpro.end())
  {
    return;
  }
  MFEM_VERIFY(inductance->is_array(),
              "\"Inductance\" should specify an array in the configuration file!");
  for (auto it = inductance->begin(); it != inductance->end(); ++it)
  {
    MFEM_VERIFY(it->find("Index") != it->end(),
                "Missing \"Inductance\" boundary \"Index\" in configuration file!");
    MFEM_VERIFY(it->find("Attributes") != it->end() && it->find("Direction") != it->end(),
                "Missing \"Attributes\" list or \"Direction\" for \"Inductance\" boundary "
                "in configuration file!");
    auto ret = mapdata.insert(std::make_pair(it->at("Index"), InductanceData()));
    MFEM_VERIFY(ret.second, "Repeated \"Index\" found when processing \"Inductance\" "
                            "boundaries in configuration file!");
    InductanceData &data = ret.first->second;
    data.attributes = it->at("Attributes").get<std::vector<int>>();  // Required
    data.direction = it->at("Direction");                            // Required
    CheckDirection(data.direction, false);

    // Debug
    // std::cout << "Index: " << ret.first->first << '\n';
    // std::cout << "Attributes: " << data.attributes << '\n';
    // std::cout << "Direction: " << data.direction << '\n';

    // Cleanup
    it->erase("Index");
    it->erase("Attributes");
    it->erase("Direction");
    MFEM_VERIFY(it->empty(),
                "Found an unsupported configuration file keyword under \"Inductance\"!\n"
                    << it->dump(2));
  }
}

void InterfaceDielectricPostData::SetUp(json &postpro)
{
  auto dielectric = postpro.find("Dielectric");
  if (dielectric == postpro.end())
  {
    return;
  }
  MFEM_VERIFY(dielectric->is_array(),
              "\"Dielectric\" should specify an array in the configuration file!");
  for (auto it = dielectric->begin(); it != dielectric->end(); ++it)
  {
    MFEM_VERIFY(it->find("Index") != it->end(),
                "Missing \"Dielectric\" boundary \"Index\" in configuration file!");
    // One (and only one) of epsilon_r, epsilon_r_ma, epsilon_r_ms, and epsilon_r_sa
    // are required for surfaces.
    MFEM_VERIFY((it->find("Permittivity") != it->end()) +
                        (it->find("PermittivityMA") != it->end()) +
                        (it->find("PermittivityMS") != it->end()) +
                        (it->find("PermittivitySA") != it->end()) ==
                    1,
                "Only one of \"Dielectric\" boundary \"Permittivity\", "
                "\"PermittivityMA\", \"PermittivityMS\", or \"PermittivitySA\" should be "
                "specified for interface dielectric loss in configuration file!");
    MFEM_VERIFY(it->find("Thickness") != it->end(),
                "Missing \"Dielectric\" boundary \"Thickness\" in configuration file!");
    auto ret = mapdata.insert(std::make_pair(it->at("Index"), InterfaceDielectricData()));
    MFEM_VERIFY(ret.second, "Repeated \"Index\" found when processing \"Dielectric\" "
                            "boundaries in configuration file!");
    InterfaceDielectricData &data = ret.first->second;
    data.ts = it->at("Thickness");  // Required for surfaces
    data.tandelta = it->value("LossTan", data.tandelta);
    data.epsilon_r = it->value("Permittivity", data.epsilon_r);
    data.epsilon_r_ma = it->value("PermittivityMA", data.epsilon_r_ma);
    data.epsilon_r_ms = it->value("PermittivityMS", data.epsilon_r_ms);
    data.epsilon_r_sa = it->value("PermittivitySA", data.epsilon_r_sa);
    if (it->find("Attributes") != it->end())
    {
      MFEM_VERIFY(it->find("Elements") == it->end(),
                  "Cannot specify both top-level \"Attributes\" list and \"Elements\" for "
                  "\"Dielectric\" boundary in configuration file!");
      data.nodes.resize(1);
      InterfaceDielectricData::Node &node = data.nodes.back();
      node.attributes = it->at("Attributes").get<std::vector<int>>();  // Required
      node.side = it->value("Side", node.side);
      if (!node.side.empty())
      {
        CheckDirection(node.side, false);  // Can be empty
      }
    }
    else
    {
      auto elements = it->find("Elements");
      MFEM_VERIFY(elements != it->end(),
                  "Missing top-level \"Attributes\" list or \"Elements\" for "
                  "\"Dielectric\" boundary in configuration file!");
      for (auto elem_it = elements->begin(); elem_it != elements->end(); ++elem_it)
      {
        MFEM_VERIFY(elem_it->find("Attributes") != elem_it->end(),
                    "Missing \"Attributes\" list for \"Dielectric\" boundary element in "
                    "configuration file!");
        data.nodes.emplace_back();
        InterfaceDielectricData::Node &node = data.nodes.back();
        node.attributes = elem_it->at("Attributes").get<std::vector<int>>();  // Required
        node.side = it->value("Side", node.side);
        if (!node.side.empty())
        {
          CheckDirection(node.side, false);  // Can be empty
        }

        // Cleanup
        elem_it->erase("Attributes");
        elem_it->erase("Side");
        MFEM_VERIFY(elem_it->empty(), "Found an unsupported configuration file keyword "
                                      "under \"Dielectric\" boundary element!\n"
                                          << elem_it->dump(2));
      }
    }

    // Debug
    // std::cout << "Index: " << ret.first->first << '\n';
    // std::cout << "LossTan: " << data.tandelta << '\n';
    // std::cout << "Permittivity: " << data.epsilon_r << '\n';
    // std::cout << "PermittivityMA: " << data.epsilon_r_ma << '\n';
    // std::cout << "PermittivityMS: " << data.epsilon_r_ms << '\n';
    // std::cout << "PermittivitySA: " << data.epsilon_r_sa << '\n';
    // std::cout << "Thickness: " << data.ts << '\n';
    // for (const auto &node : data.nodes)
    // {
    //   std::cout << "Attributes: " << node.attributes << '\n';
    //   std::cout << "Side: " << node.side << '\n';
    // }

    // Cleanup
    it->erase("Index");
    it->erase("LossTan");
    it->erase("Permittivity");
    it->erase("PermittivityMA");
    it->erase("PermittivityMS");
    it->erase("PermittivitySA");
    it->erase("Thickness");
    it->erase("Attributes");
    it->erase("Side");
    MFEM_VERIFY(it->empty(),
                "Found an unsupported configuration file keyword under \"Dielectric\"!\n"
                    << it->dump(2));
  }
}

void BoundaryPostData::SetUp(json &boundaries)
{
  auto postpro = boundaries.find("Postprocessing");
  if (postpro == boundaries.end())
  {
    return;
  }
  capacitance.SetUp(*postpro);
  inductance.SetUp(*postpro);
  dielectric.SetUp(*postpro);

  // Store all unique postprocessing boundary attributes.
  for (const auto &[idx, data] : capacitance)
  {
    attributes.insert(data.attributes.begin(), data.attributes.end());
  }
  for (const auto &[idx, data] : inductance)
  {
    attributes.insert(data.attributes.begin(), data.attributes.end());
  }
  for (const auto &[idx, data] : dielectric)
  {
    for (const auto &node : data.nodes)
    {
      attributes.insert(node.attributes.begin(), node.attributes.end());
    }
  }

  // Cleanup
  postpro->erase("Capacitance");
  postpro->erase("Inductance");
  postpro->erase("Dielectric");
  MFEM_VERIFY(postpro->empty(),
              "Found an unsupported configuration file keyword under \"Postprocessing\"!\n"
                  << postpro->dump(2));
}

void BoundaryData::SetUp(json &config)
{
  auto boundaries = config.find("Boundaries");
  MFEM_VERIFY(boundaries != config.end(),
              "\"Boundaries\" must be specified in configuration file!");
  pec.SetUp(*boundaries);
  pmc.SetUp(*boundaries);
  auxpec.SetUp(*boundaries);
  farfield.SetUp(*boundaries);
  conductivity.SetUp(*boundaries);
  impedance.SetUp(*boundaries);
  lumpedport.SetUp(*boundaries);
  waveport.SetUp(*boundaries);
  current.SetUp(*boundaries);
  postpro.SetUp(*boundaries);

  // Store all unique boundary attributes.
  attributes.insert(pec.attributes.begin(), pec.attributes.end());
  attributes.insert(pmc.attributes.begin(), pmc.attributes.end());
  attributes.insert(auxpec.attributes.begin(), auxpec.attributes.end());
  attributes.insert(farfield.attributes.begin(), farfield.attributes.end());
  for (const auto &data : conductivity)
  {
    attributes.insert(data.attributes.begin(), data.attributes.end());
  }
  for (const auto &data : impedance)
  {
    attributes.insert(data.attributes.begin(), data.attributes.end());
  }
  for (const auto &[idx, data] : lumpedport)
  {
    for (const auto &node : data.nodes)
    {
      attributes.insert(node.attributes.begin(), node.attributes.end());
    }
  }
  for (const auto &[idx, data] : waveport)
  {
    attributes.insert(data.attributes.begin(), data.attributes.end());
  }
  for (const auto &[idx, data] : current)
  {
    for (const auto &node : data.nodes)
    {
      attributes.insert(node.attributes.begin(), node.attributes.end());
    }
  }
  attributes.insert(postpro.attributes.begin(), postpro.attributes.end());

  // Cleanup
  boundaries->erase("PEC");
  boundaries->erase("PMC");
  boundaries->erase("WavePortPEC");
  boundaries->erase("Absorbing");
  boundaries->erase("Conductivity");
  boundaries->erase("Impedance");
  boundaries->erase("LumpedPort");
  boundaries->erase("WavePort");
  boundaries->erase("SurfaceCurrent");
  boundaries->erase("Ground");
  boundaries->erase("ZeroCharge");
  boundaries->erase("Terminal");
  boundaries->erase("Postprocessing");
  MFEM_VERIFY(boundaries->empty(),
              "Found an unsupported configuration file keyword under \"Boundaries\"!\n"
                  << boundaries->dump(2));
}

void DrivenSolverData::SetUp(json &solver)
{
  auto driven = solver.find("Driven");
  if (driven == solver.end())
  {
    return;
  }
  MFEM_VERIFY(driven->find("MinFreq") != driven->end() &&
                  driven->find("MaxFreq") != driven->end() &&
                  driven->find("FreqStep") != driven->end(),
              "Missing \"Driven\" solver \"MinFreq\", \"MaxFreq\", or \"FreqStep\" in "
              "configuration file!");
  min_f = driven->at("MinFreq");     // Required
  max_f = driven->at("MaxFreq");     // Required
  delta_f = driven->at("FreqStep");  // Required
  delta_post = driven->value("SaveStep", delta_post);
  only_port_post = driven->value("SaveOnlyPorts", only_port_post);
  adaptive_tol = driven->value("AdaptiveTol", adaptive_tol);
  adaptive_nmax = driven->value("AdaptiveMaxSamples", adaptive_nmax);
  adaptive_ncand = driven->value("AdaptiveMaxCandidates", adaptive_ncand);
  adaptive_metric_aposteriori =
      driven->value("AdaptiveAPosterioriError", adaptive_metric_aposteriori);
  rst = driven->value("Restart", rst);

  // Cleanup
  driven->erase("MinFreq");
  driven->erase("MaxFreq");
  driven->erase("FreqStep");
  driven->erase("SaveStep");
  driven->erase("SaveOnlyPorts");
  driven->erase("AdaptiveTol");
  driven->erase("AdaptiveMaxSamples");
  driven->erase("AdaptiveMaxCandidates");
  driven->erase("AdaptiveAPosterioriError");
  driven->erase("Restart");
  MFEM_VERIFY(driven->empty(),
              "Found an unsupported configuration file keyword under \"Driven\"!\n"
                  << driven->dump(2));

  // Debug
  // std::cout << "MinFreq: " << min_f << '\n';
  // std::cout << "MaxFreq: " << max_f << '\n';
  // std::cout << "FreqStep: " << delta_f << '\n';
  // std::cout << "SaveStep: " << delta_post << '\n';
  // std::cout << "SaveOnlyPorts: " << only_port_post << '\n';
  // std::cout << "AdaptiveTol: " << adaptive_tol << '\n';
  // std::cout << "AdaptiveMaxSamples: " << adaptive_nmax << '\n';
  // std::cout << "AdaptiveMaxCandidates: " << adaptive_ncand << '\n';
  // std::cout << "AdaptiveAPosterioriError: " << adaptive_metric_aposteriori << '\n';
  // std::cout << "Restart: " << rst << '\n';
}

// Helper for converting string keys to enum for EigenSolverData::Type.
NLOHMANN_JSON_SERIALIZE_ENUM(EigenSolverData::Type,
                             {{EigenSolverData::Type::INVALID, nullptr},
                              {EigenSolverData::Type::ARPACK, "ARPACK"},
                              {EigenSolverData::Type::SLEPC, "SLEPc"},
                              {EigenSolverData::Type::FEAST, "FEAST"},
                              {EigenSolverData::Type::DEFAULT, "Default"}})

void EigenSolverData::SetUp(json &solver)
{
  auto eigenmode = solver.find("Eigenmode");
  if (eigenmode == solver.end())
  {
    return;
  }
  MFEM_VERIFY(eigenmode->find("Target") != eigenmode->end(),
              "Missing \"Eigenmode\" solver \"Target\" in configuration file!");
  target = eigenmode->at("Target");  // Required
  tol = eigenmode->value("Tol", tol);
  max_it = eigenmode->value("MaxIts", max_it);
  max_size = eigenmode->value("MaxSize", max_size);
  n = eigenmode->value("N", n);
  n_post = eigenmode->value("Save", n_post);
  type = eigenmode->value("Type", type);
  MFEM_VERIFY(type != EigenSolverData::Type::INVALID,
              "Invalid value for config[\"Eigenmode\"][\"Type\"] in configuration file!");
  pep_linear = eigenmode->value("PEPLinear", pep_linear);
  feast_contour_np = eigenmode->value("ContourNPoints", feast_contour_np);
  if (type == EigenSolverData::Type::FEAST && feast_contour_np > 1)
  {
    MFEM_VERIFY(eigenmode->find("ContourTargetUpper") != eigenmode->end() &&
                    eigenmode->find("ContourAspectRatio") != eigenmode->end(),
                "Missing \"Eigenmode\" solver \"ContourTargetUpper\" or "
                "\"ContourAspectRatio\" for FEAST solver in configuration file!");
  }
  feast_contour_ub = eigenmode->value("ContourTargetUpper", feast_contour_ub);
  feast_contour_ar = eigenmode->value("ContourAspectRatio", feast_contour_ar);
  feast_moments = eigenmode->value("ContourMoments", feast_moments);
  scale = eigenmode->value("Scaling", scale);
  init_v0 = eigenmode->value("StartVector", init_v0);
  init_v0_const = eigenmode->value("StartVectorConstant", init_v0_const);
  mass_orthog = eigenmode->value("MassOrthogonal", mass_orthog);

  // Cleanup
  eigenmode->erase("Target");
  eigenmode->erase("Tol");
  eigenmode->erase("MaxIts");
  eigenmode->erase("MaxSize");
  eigenmode->erase("N");
  eigenmode->erase("Save");
  eigenmode->erase("Type");
  eigenmode->erase("PEPLinear");
  eigenmode->erase("ContourNPoints");
  eigenmode->erase("ContourTargetUpper");
  eigenmode->erase("ContourAspectRatio");
  eigenmode->erase("ContourMoments");
  eigenmode->erase("Scaling");
  eigenmode->erase("StartVector");
  eigenmode->erase("StartVectorConstant");
  eigenmode->erase("MassOrthogonal");
  MFEM_VERIFY(eigenmode->empty(),
              "Found an unsupported configuration file keyword under \"Eigenmode\"!\n"
                  << eigenmode->dump(2));

  // Debug
  // std::cout << "Target: " << target << '\n';
  // std::cout << "Tol: " << tol << '\n';
  // std::cout << "MaxIts: " << max_it << '\n';
  // std::cout << "MaxSize: " << max_size << '\n';
  // std::cout << "N: " << n << '\n';
  // std::cout << "Save: " << n_post << '\n';
  // std::cout << "Type: " << type << '\n';
  // std::cout << "PEPLinear: " << pep_linear << '\n';
  // std::cout << "ContourNPoints: " << feast_contour_np << '\n';
  // std::cout << "ContourTargetUpper: " << feast_contour_ub << '\n';
  // std::cout << "ContourAspectRatio: " << feast_contour_ar << '\n';
  // std::cout << "ContourMoments: " << feast_moments << '\n';
  // std::cout << "Scaling: " << scale << '\n';
  // std::cout << "StartVector: " << init_v0 << '\n';
  // std::cout << "StartVectorConstant: " << init_v0_const << '\n';
  // std::cout << "MassOrthogonal: " << mass_orthog << '\n';
}

void ElectrostaticSolverData::SetUp(json &solver)
{
  auto electrostatic = solver.find("Electrostatic");
  if (electrostatic == solver.end())
  {
    return;
  }
  n_post = electrostatic->value("Save", n_post);

  // Cleanup
  electrostatic->erase("Save");
  MFEM_VERIFY(electrostatic->empty(),
              "Found an unsupported configuration file keyword under \"Electrostatic\"!\n"
                  << electrostatic->dump(2));

  // Debug
  // std::cout << "Save: " << n_post << '\n';
}

void MagnetostaticSolverData::SetUp(json &solver)
{
  auto magnetostatic = solver.find("Magnetostatic");
  if (magnetostatic == solver.end())
  {
    return;
  }
  n_post = magnetostatic->value("Save", n_post);

  // Cleanup
  magnetostatic->erase("Save");
  MFEM_VERIFY(magnetostatic->empty(),
              "Found an unsupported configuration file keyword under \"Magnetostatic\"!\n"
                  << magnetostatic->dump(2));

  // Debug
  // std::cout << "Save: " << n_post << '\n';
}

// Helper for converting string keys to enum for TransientSolverData::Type and
// TransientSolverData::ExcitationType.
NLOHMANN_JSON_SERIALIZE_ENUM(TransientSolverData::Type,
                             {{TransientSolverData::Type::INVALID, nullptr},
                              {TransientSolverData::Type::GEN_ALPHA, "GeneralizedAlpha"},
                              {TransientSolverData::Type::NEWMARK, "NewmarkBeta"},
                              {TransientSolverData::Type::CENTRAL_DIFF,
                               "CentralDifference"},
                              {TransientSolverData::Type::DEFAULT, "Default"}})
NLOHMANN_JSON_SERIALIZE_ENUM(
    TransientSolverData::ExcitationType,
    {{TransientSolverData::ExcitationType::INVALID, nullptr},
     {TransientSolverData::ExcitationType::SINUSOIDAL, "Sinusoidal"},
     {TransientSolverData::ExcitationType::GAUSSIAN, "Gaussian"},
     {TransientSolverData::ExcitationType::DIFF_GAUSSIAN, "DifferentiatedGaussian"},
     {TransientSolverData::ExcitationType::MOD_GAUSSIAN, "ModulatedGaussian"},
     {TransientSolverData::ExcitationType::RAMP_STEP, "Ramp"},
     {TransientSolverData::ExcitationType::SMOOTH_STEP, "SmoothStep"}})

void TransientSolverData::SetUp(json &solver)
{
  auto transient = solver.find("Transient");
  if (transient == solver.end())
  {
    return;
  }
  MFEM_VERIFY(transient->find("Excitation") != transient->end(),
              "Missing \"Transient\" solver \"Excitation\" type in configuration file!");
  MFEM_VERIFY(
      transient->find("MaxTime") != transient->end() &&
          transient->find("TimeStep") != transient->end(),
      "Missing \"Transient\" solver \"MaxTime\" or \"TimeStep\" in configuration file!");
  type = transient->value("Type", type);
  MFEM_VERIFY(type != TransientSolverData::Type::INVALID,
              "Invalid value for config[\"Transient\"][\"Type\"] in configuration file!");
  excitation = transient->at("Excitation");  // Required
  MFEM_VERIFY(
      excitation != TransientSolverData::ExcitationType::INVALID,
      "Invalid value for config[\"Transient\"][\"Excitation\"] in configuration file!");
  pulse_f = transient->value("ExcitationFreq", pulse_f);
  pulse_tau = transient->value("ExcitationWidth", pulse_tau);
  max_t = transient->at("MaxTime");     // Required
  delta_t = transient->at("TimeStep");  // Required
  delta_post = transient->value("SaveStep", delta_post);
  only_port_post = transient->value("SaveOnlyPorts", only_port_post);

  // Cleanup
  transient->erase("Type");
  transient->erase("Excitation");
  transient->erase("ExcitationFreq");
  transient->erase("ExcitationWidth");
  transient->erase("MaxTime");
  transient->erase("TimeStep");
  transient->erase("SaveStep");
  transient->erase("SaveOnlyPorts");
  MFEM_VERIFY(transient->empty(),
              "Found an unsupported configuration file keyword under \"Transient\"!\n"
                  << transient->dump(2));

  // Debug
  // std::cout << "Type: " << type << '\n';
  // std::cout << "Excitation: " << excitation << '\n';
  // std::cout << "ExcitationFreq: " << pulse_freq << '\n';
  // std::cout << "ExcitationWidth: " << pulse_tau << '\n';
  // std::cout << "MaxTime: " << max_t << '\n';
  // std::cout << "TimeStep: " << delta_t << '\n';
  // std::cout << "SaveStep: " << delta_post << '\n';
  // std::cout << "SaveOnlyPorts: " << only_port_post << '\n';
}

// Helpers for converting string keys to enum for LinearSolverData::Type,
// LinearSolverData::Ksp, LinearSolverData::SideType, and
// LinearSolverData::CompressionType.
NLOHMANN_JSON_SERIALIZE_ENUM(LinearSolverData::Type,
                             {{LinearSolverData::Type::INVALID, nullptr},
                              {LinearSolverData::Type::AMS, "AMS"},
                              {LinearSolverData::Type::BOOMER_AMG, "BoomerAMG"},
                              {LinearSolverData::Type::MUMPS, "MUMPS"},
                              {LinearSolverData::Type::SUPERLU, "SuperLU"},
                              {LinearSolverData::Type::STRUMPACK, "STRUMPACK"},
                              {LinearSolverData::Type::STRUMPACK_MP, "STRUMPACK-MP"},
                              {LinearSolverData::Type::DEFAULT, "Default"}})
NLOHMANN_JSON_SERIALIZE_ENUM(LinearSolverData::KspType,
                             {{LinearSolverData::KspType::INVALID, nullptr},
                              {LinearSolverData::KspType::CG, "CG"},
                              {LinearSolverData::KspType::CGSYM, "CGSYM"},
                              {LinearSolverData::KspType::FCG, "FCG"},
                              {LinearSolverData::KspType::MINRES, "MINRES"},
                              {LinearSolverData::KspType::GMRES, "GMRES"},
                              {LinearSolverData::KspType::FGMRES, "FGMRES"},
                              {LinearSolverData::KspType::BCGS, "BCGS"},
                              {LinearSolverData::KspType::BCGSL, "BCGSL"},
                              {LinearSolverData::KspType::FBCGS, "FBCGS"},
                              {LinearSolverData::KspType::QMRCGS, "QMRCGS"},
                              {LinearSolverData::KspType::TFQMR, "TFQMR"},
                              {LinearSolverData::KspType::DEFAULT, "Default"}})
NLOHMANN_JSON_SERIALIZE_ENUM(LinearSolverData::SideType,
                             {{LinearSolverData::SideType::INVALID, nullptr},
                              {LinearSolverData::SideType::RIGHT, "Right"},
                              {LinearSolverData::SideType::LEFT, "Left"},
                              {LinearSolverData::SideType::DEFAULT, "Default"}})
NLOHMANN_JSON_SERIALIZE_ENUM(LinearSolverData::SymFactType,
                             {{LinearSolverData::SymFactType::INVALID, nullptr},
                              {LinearSolverData::SymFactType::METIS, "METIS"},
                              {LinearSolverData::SymFactType::PARMETIS, "ParMETIS"},
                              {LinearSolverData::SymFactType::DEFAULT, "Default"}})
NLOHMANN_JSON_SERIALIZE_ENUM(LinearSolverData::CompressionType,
                             {{LinearSolverData::CompressionType::INVALID, nullptr},
                              {LinearSolverData::CompressionType::NONE, "None"},
                              {LinearSolverData::CompressionType::BLR, "BLR"},
                              {LinearSolverData::CompressionType::HSS, "HSS"},
                              {LinearSolverData::CompressionType::HODLR, "HODLR"},
                              {LinearSolverData::CompressionType::ZFP, "ZFP"},
                              {LinearSolverData::CompressionType::BLR_HODLR, "BLR-HODLR"},
                              {LinearSolverData::CompressionType::ZFP_BLR_HODLR,
                               "ZFP-BLR-HODLR"}})

void LinearSolverData::SetUp(json &solver)
{
  auto linear = solver.find("Linear");
  if (linear == solver.end())
  {
    return;
  }
  type = linear->value("Type", type);
  MFEM_VERIFY(type != LinearSolverData::Type::INVALID,
              "Invalid value for config[\"Linear\"][\"Type\"] in configuration file!");
  ksp_type = linear->value("KSPType", ksp_type);
  MFEM_VERIFY(ksp_type != LinearSolverData::KspType::INVALID,
              "Invalid value for config[\"Linear\"][\"KSPType\"] in configuration file!");
  tol = linear->value("Tol", tol);
  max_it = linear->value("MaxIts", max_it);
  max_size = linear->value("MaxSize", max_size);
  orthog_mgs = linear->value("UseMGS", orthog_mgs);
  orthog_cgs2 = linear->value("UseCGS2", orthog_cgs2);
  ksp_initial_guess = linear->value("UseInitialGuess", ksp_initial_guess);
  ksp_piped = linear->value("UseKSPPiped", ksp_piped);

  // Preconditioner-specific options
  mat_gmg = linear->value("UseGMG", mat_gmg);
  mat_lor = linear->value("UseLOR", mat_lor);
  mat_shifted = linear->value("UsePCShifted", mat_shifted);
  mg_cycle_it = linear->value("MGCycleIts", mg_cycle_it);
  mg_smooth_it = linear->value("MGSmoothIts", mg_smooth_it);
  mg_smooth_order = linear->value("MGSmoothOrder", mg_smooth_order);
  pc_side_type = linear->value("PrecondSide", pc_side_type);
  MFEM_VERIFY(
      pc_side_type != LinearSolverData::SideType::INVALID,
      "Invalid value for config[\"Linear\"][\"PrecondSide\"] in configuration file!");
  sym_fact_type = linear->value("Reordering", sym_fact_type);
  MFEM_VERIFY(
      sym_fact_type != LinearSolverData::SymFactType::INVALID,
      "Invalid value for config[\"Linear\"][\"Reordering\"] in configuration file!");
  strumpack_compression_type =
      linear->value("STRUMPACKCompressionType", strumpack_compression_type);
  MFEM_VERIFY(strumpack_compression_type != LinearSolverData::CompressionType::INVALID,
              "Invalid value for config[\"Linear\"][\"STRUMPACKCompressionType\"] in "
              "configuration file!");
  strumpack_lr_tol = linear->value("STRUMPACKCompressionTol", strumpack_lr_tol);
  strumpack_lossy_precision =
      linear->value("STRUMPACKLossyPrecision", strumpack_lossy_precision);
  strumpack_butterfly_l = linear->value("STRUMPACKButterflyLevels", strumpack_butterfly_l);
  superlu_3d = linear->value("SuperLU3D", superlu_3d);
  ams_vector = linear->value("AMSVector", ams_vector);
  divfree_tol = linear->value("DivFreeTol", divfree_tol);
  divfree_max_it = linear->value("DivFreeMaxIts", divfree_max_it);

  // Cleanup
  linear->erase("Type");
  linear->erase("KSPType");
  linear->erase("Tol");
  linear->erase("MaxIts");
  linear->erase("MaxSize");
  linear->erase("UseMGS");
  linear->erase("UseCGS2");
  linear->erase("UseInitialGuess");
  linear->erase("UseKSPPiped");
  linear->erase("UseGMG");
  linear->erase("UseLOR");
  linear->erase("UsePCShifted");
  linear->erase("MGCycleIts");
  linear->erase("MGSmoothIts");
  linear->erase("MGSmoothOrder");
  linear->erase("PrecondSide");
  linear->erase("Reordering");
  linear->erase("STRUMPACKCompressionType");
  linear->erase("STRUMPACKCompressionTol");
  linear->erase("STRUMPACKLossyPrecision");
  linear->erase("STRUMPACKButterflyLevels");
  linear->erase("SuperLU3D");
  linear->erase("AMSVector");
  linear->erase("DivFreeTol");
  linear->erase("DivFreeMaxIts");
  MFEM_VERIFY(linear->empty(),
              "Found an unsupported configuration file keyword under \"Linear\"!\n"
                  << linear->dump(2));

  // Debug
  // std::cout << "Type: " << type << '\n';
  // std::cout << "KSPType: " << ksp_type << '\n';
  // std::cout << "Tol: " << tol << '\n';
  // std::cout << "MaxIts: " << max_it << '\n';
  // std::cout << "MaxSize: " << max_size << '\n';
  // std::cout << "UseMGS: " << orthog_mgs << '\n';
  // std::cout << "UseCGS2: " << orthog_cgs2 << '\n';
  // std::cout << "UseInitialGuess: " << ksp_initial_guess << '\n';
  // std::cout << "UseKSPPiped: " << ksp_piped << '\n';
  // std::cout << "UseGMG: " << mat_gmg << '\n';
  // std::cout << "UseLOR: " << mat_lor << '\n';
  // std::cout << "UsePCShifted: " << mat_shifted << '\n';
  // std::cout << "MGCycleIts: " << mg_cycle_it << '\n';
  // std::cout << "MGSmoothIts: " << mg_smooth_it << '\n';
  // std::cout << "MGSmoothOrder: " << mg_smooth_order << '\n';
  // std::cout << "PrecondSide: " << pc_side_type << '\n';
  // std::cout << "Reordering: " << sym_fact_type << '\n';
  // std::cout << "STRUMPACKCompressionType: " << strumpack_compression_type << '\n';
  // std::cout << "STRUMPACKCompressionTol: " << strumpack_lr_tol << '\n';
  // std::cout << "STRUMPACKLossyPrecision: " << strumpack_lossy_precision << '\n';
  // std::cout << "STRUMPACKButterflyLevels: " << strumpack_butterfly_l << '\n';
  // std::cout << "SuperLU3D: " << superlu_3d << '\n';
  // std::cout << "AMSVector: " << ams_vector << '\n';
  // std::cout << "DivFreeTol: " << divfree_tol << '\n';
  // std::cout << "DivFreeMaxIts: " << divfree_max_it << '\n';
}

void SolverData::SetUp(json &config)
{
  auto solver = config.find("Solver");
  if (solver == config.end())
  {
    return;
  }
  order = solver->value("Order", order);
  MFEM_VERIFY(order > 0, "config[\"Solver\"][\"Order\"] must be positive!");
  driven.SetUp(*solver);
  eigenmode.SetUp(*solver);
  electrostatic.SetUp(*solver);
  magnetostatic.SetUp(*solver);
  transient.SetUp(*solver);
  linear.SetUp(*solver);

  // Cleanup
  solver->erase("Order");
  solver->erase("Driven");
  solver->erase("Eigenmode");
  solver->erase("Electrostatic");
  solver->erase("Magnetostatic");
  solver->erase("Transient");
  solver->erase("Linear");
  MFEM_VERIFY(solver->empty(),
              "Found an unsupported configuration file keyword under \"Solver\"!\n"
                  << solver->dump(2));

  // Debug
  // std::cout << "Order: " << order << '\n';
}

}  // namespace palace::config
