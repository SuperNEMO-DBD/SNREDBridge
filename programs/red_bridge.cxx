// Standard library:
#include <cstdio>
#include <iostream>
#include <exception>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

// Third party:
// - Bayeux:
#include <bayeux/datatools/clhep_units.h>
#include <bayeux/datatools/logger.h>
#include <bayeux/datatools/io_factory.h>
#include <bayeux/datatools/properties.h>
#include <bayeux/datatools/things.h>
#include <bayeux/dpp/output_module.h>

// - Falaise:
#include <falaise/snemo/datamodels/event_header.h>
#include <falaise/snemo/datamodels/geomid_utils.h>
#include <falaise/snemo/datamodels/unified_digitized_data.h>

// - SNFEE:
#include <snfee/snfee.h>
#include <snfee/io/multifile_data_reader.h>
#include <snfee/data/raw_event_data.h>
#include <snfee/data/time.h>

// global variables
bool no_waveform = false;
double run_sync_time = 0;
snemo::datamodel::timestamp previous_eh_timestamp;
datatools::logger::priority logging = datatools::logger::PRIO_WARNING;

void do_red_to_udd_conversion(const snfee::data::raw_event_data,
                              datatools::things &);

//----------------------------------------------------------------------
// MAIN PROGRAM
//----------------------------------------------------------------------

int main (int argc, char *argv[])
{
  int error_code = EXIT_SUCCESS;
  try {
  std::string input_filename = "";
  std::string output_filename = "";
  size_t data_count = 100000000;

  for (int iarg=1; iarg<argc; ++iarg)
    {
      std::string arg (argv[iarg]);
      if (arg[0] == '-')
        {
          if ((arg == "-d") || (arg == "--debug"))
            logging = datatools::logger::PRIO_DEBUG;

          else if ((arg == "-v") || (arg == "--verbose"))
            logging = datatools::logger::PRIO_INFORMATION;

          else if ((arg=="-i") || (arg=="--input"))
            input_filename = std::string(argv[++iarg]);

          else if ((arg=="-o") || (arg=="--output"))
            output_filename = std::string(argv[++iarg]);

          else if ((arg == "-n") || (arg == "--max-events"))
            data_count = std::strtol(argv[++iarg], NULL, 10);

          else if ((arg == "-no-wf") || (arg == "--no-waveform"))
            no_waveform = true;

          else if ((arg == "--sync-time") || (arg == "-s"))
	    run_sync_time = std::strtod(argv[++iarg], NULL);

          else if (arg=="-h" || arg=="--help")
            {
              std::cout << std::endl;
              std::cout << "Usage:   " << argv[0] << " [options]" << std::endl;
              std::cout << std::endl;
              std::cout << "Options:   -h / --help" << std::endl;
              std::cout << "           -i / --input       RED_FILE" << std::endl;
              std::cout << "           -o / --output      UDD_FILE" << std::endl;
              std::cout << "           -n / --max-events  Max number of events" << std::endl;
              std::cout << "           -no-wf / --no-waveform Do not save the waveform from RED to UDD" << std::endl;
              std::cout << "           -v / --verbose     More logs" << std::endl;
              std::cout << "           -d / --debug       Debug logs" << std::endl;
              std::cout << std::endl;
              return 0;
            }

          else
            DT_LOG_WARNING(logging, "Ignoring option '" << arg << "' !");
        }
    }

  if (input_filename.empty())
    {
      std::cerr << "*** ERROR: missing input filename !" << std::endl;
      return 1;
    }

  if (run_sync_time == 0)
    {
      // call the DB to find run SYNC time
      std::cerr << "*** ERROR: missing start time (-s/--start-time UNIX.TIME)!" << std::endl;
      return 1;
    }

  DT_LOG_INFORMATION(logging, "SNREDBridge program : converting SNFEE RED into Falaise datatools::things event record containing EH and UDD banks for each event");

  DT_LOG_DEBUG(logging, "Initialize SNFEE");
  snfee::initialize();

  /// Configuration for raw data reader
  snfee::io::multifile_data_reader::config_type reader_cfg;
  reader_cfg.filenames.push_back(input_filename);

  // Declare the reader
  DT_LOG_DEBUG(logging, "Instantiate the RED reader");
  snfee::io::multifile_data_reader red_source(reader_cfg);

  // Declare the writer
  DT_LOG_DEBUG(logging, "Instantiate the DPP writer output module");

  // The output module:
  dpp::output_module writer;
  writer.set_logging_priority(datatools::logger::PRIO_FATAL);
  writer.set_name("Writer output module");
  writer.set_description("Output module for the datatools::things event_record");
  writer.set_preserve_existing_output(false); // Allowed to erase existing output file
  writer.set_single_output_file(output_filename);

  writer.initialize_simple();
  DT_LOG_DEBUG(logging, "Initialization of the output module is done.");


  // RED counter
  std::size_t red_counter = 0;

  // UDD counter
  std::size_t udd_counter = 0;

  while (red_source.has_record_tag() && red_counter < data_count)
    {
      // Check the serialization tag of the next record:
      DT_THROW_IF(!red_source.record_tag_is(snfee::data::raw_event_data::SERIAL_TAG),
                  std::logic_error, "Unexpected record tag '" << red_source.get_record_tag() << "'!");

      // Empty working RED object
      snfee::data::raw_event_data red;

      // Load the next RED object:
      red_source.load(red);
      red_counter++;

      // Declare a ``datatools::things`` event record
      DT_LOG_DEBUG(logging, "Declare the datatools::things event record");
      datatools::things event_record;
      std::ostringstream namess;
      namess << "ER_" << udd_counter;
      event_record.set_name(namess.str());
      event_record.set_description("An event record composed by an Event Header (EH) and the Unified Digitized Data (UDD) banks");

      // Do the RED to UDD conversion and fill the Event record
      do_red_to_udd_conversion(red, event_record);

      dpp::base_module::process_status status = writer.process(event_record);

      udd_counter++;
      DT_LOG_DEBUG(logging, "Exit do_red_to_udd_conversion");

      // Smart print :
      // event_record.tree_dump(std::clog, "The event data record composed by EH and UDD banks.");


    } // (while red_source.has_record_tag())


  // Check input RED file and output UDD file and count the number of events in each file
  // In validation program

  // Read UDD file here


  std::cout << "Results :" << std::endl;
  std::cout << "- Worker #0 (input RED)"  << std::endl;
  std::cout << "  - Processed records : " << red_counter << std::endl;
  std::cout << "- Worker #1 (output UDD)" << std::endl;
  std::cout << "  - Stored records    : " << udd_counter << std::endl;

  snfee::terminate();

  DT_LOG_INFORMATION(logging, "The end.");
  }


  catch (std::exception & x) {
    DT_LOG_FATAL(logging, x.what());
    error_code = EXIT_FAILURE;
  }
  catch (...) {
    DT_LOG_FATAL(logging, "unexpected error !");
    error_code = EXIT_FAILURE;
  }
  return (error_code);
}




void do_red_to_udd_conversion(const snfee::data::raw_event_data red_,
                              datatools::things & event_record_)
{
  // Run number
  int32_t red_run_id   = red_.get_run_id();

  // Event number
  int32_t red_event_id = red_.get_event_id();

  // Container of merged TriggerID(s) by event builder
  const std::set<int32_t> & red_trigger_ids = red_.get_origin_trigger_ids();

  // RED Digitized trigger hits
  const std::vector<snfee::data::trigger_record> red_trigger_hits = red_.get_trigger_records();

  // RED Digitized calo hits
  const std::vector<snfee::data::calo_digitized_hit> red_calo_hits = red_.get_calo_hits();

  // RED Digitized tracker hits
  const std::vector<snfee::data::tracker_digitized_hit> red_tracker_hits = red_.get_tracker_hits();

  // Print RED infos
  // std::cout << "Event #" << red_event_id << " contains "
  //           << red_trigger_ids.size() << " TriggerID(s) with "
  //           << red_calo_hits.size() << " calo hit(s) and "
  //           << red_tracker_hits.size() << " tracker hit(s)"
  //           << std::endl;

  std::string EH_output_tag  = "EH";
  std::string UDD_output_tag = "UDD";

  // Empty working EH object
  // auto & EH = snedm::addToEvent<snemo::datamodel::event_header>(EH_output_tag, event_record_);
  auto & EH = event_record_.add<snemo::datamodel::event_header>(EH_output_tag);

  // Empty working UDD object
  // auto & UDD = snedm::addToEvent<snemo::datamodel::unified_digitized_data>(UDD_output_tag, event_record_);
  auto & UDD = event_record_.add<snemo::datamodel::unified_digitized_data>(UDD_output_tag);

  // Fill Event Header based on RED attributes
  EH.get_id().set_run_number(red_run_id);
  EH.get_id().set_event_number(red_event_id);
  EH.set_generation(snemo::datamodel::event_header::GENERATION_REAL);

  // Set the event timestamp
  const snfee::data::timestamp & reference_timestamp = red_.get_reference_time();
  const double reference_time = reference_timestamp.get_ticks() * snfee::data::clock_period(reference_timestamp.get_clock());
  const double event_time = run_sync_time + reference_time/CLHEP::second;
  const int64_t event_time_sec = std::floor(event_time);
  const int64_t event_time_psec = std::floor(1E12*(event_time-event_time_sec));
  EH.get_timestamp().set_seconds(event_time_sec);
  EH.get_timestamp().set_picoseconds(event_time_psec);

  // Transfer RED properties to EH one
  EH.set_properties(red_.get_auxiliaries());

  // Compute and store deltat to the previous event
  const snemo::datamodel::timestamp & eh_timestamp = EH.get_timestamp();
  double deltat_previous_event = 0;
  if (previous_eh_timestamp.is_valid()) {
    deltat_previous_event = eh_timestamp.get_seconds() - previous_eh_timestamp.get_seconds();
    deltat_previous_event += 1E-12 * (eh_timestamp.get_picoseconds() - previous_eh_timestamp.get_picoseconds());
  }

  if (deltat_previous_event < 0)
    DT_LOG_WARNING(logging, "negative deltat (" << deltat_previous_event << " sec) for event #" << red_event_id);

  EH.get_properties().store("deltat_previous_event", deltat_previous_event*CLHEP::second);
  previous_eh_timestamp = EH.get_timestamp();

  // // Store event time width
  // if (red_.get_auxiliaries().has_key("time_width"))
  //   EH.get_properties().store_real("time_width", red_.get_auxiliaries().fetch_real("time_width"));

  // // Store first hit time
  // if (red_.get_auxiliaries().has_key("first_hit_time"))
  //   EH.get_properties().store_real("first_hit_time", red_.get_auxiliaries().fetch_real("first_hit_time"));

  // Store trigger info
  datatools::properties::data::vint trigger_id_vint;
  datatools::properties::data::vint trigger_decision_vint;
  datatools::properties::data::vint progenitor_trigger_id_vint;

  for (const auto & red_trigger_hit : red_trigger_hits) {
    trigger_id_vint.push_back(red_trigger_hit.get_trigger_id());
    trigger_decision_vint.push_back(red_trigger_hit.get_trigger_decision());
    if (red_trigger_hit.has_progenitor_trigger_id())
      progenitor_trigger_id_vint.push_back(red_trigger_hit.get_progenitor_trigger_id());
    else progenitor_trigger_id_vint.push_back(-1);
  }

  EH.get_properties().store("trigger_id", trigger_id_vint);
  EH.get_properties().store("trigger_decision", trigger_decision_vint);
  EH.get_properties().store("progenitor_trigger_id", progenitor_trigger_id_vint);

  // GO: we can add some additional properties to the Event Header
  // EH.get_properties().store("simulation.bundle", "falaise");
  // EH.get_properties().store("simulation.version", "0.1");
  // EH.get_properties().store("author", std::string(getenv("USER")));

  // Copy RED attributes to UDD attributes
  UDD.set_run_id(red_run_id);
  UDD.set_event_id(red_event_id);
  UDD.set_reference_timestamp(reference_timestamp.get_ticks());
  UDD.set_origin_trigger_ids(red_trigger_ids);
  // UDD.set_auxiliaries(red_.get_auxiliaries());

  // Scan and copy RED calo digitized hit into UDD calo digitized hit:
  for (std::size_t ihit = 0; ihit < red_calo_hits.size(); ihit++)
    {
      // std::clog << "DEBUG do_red_to_udd_conversion : Calo hit #" << ihit << std::endl;
      snfee::data::calo_digitized_hit red_calo_hit = red_calo_hits[ihit];
      snemo::datamodel::calorimeter_digitized_hit & udd_calo_hit = UDD.add_calorimeter_hit();
      udd_calo_hit.set_geom_id(red_calo_hit.get_geom_id());
      udd_calo_hit.set_hit_id(red_calo_hit.get_hit_id());
      udd_calo_hit.set_timestamp(red_calo_hit.get_reference_time().get_ticks());
      std::vector<int16_t> calo_waveform = red_calo_hit.get_waveform();
      if (!no_waveform) udd_calo_hit.set_waveform(calo_waveform);
      udd_calo_hit.set_low_threshold_only(red_calo_hit.is_low_threshold_only());
      udd_calo_hit.set_high_threshold(red_calo_hit.is_high_threshold());
      udd_calo_hit.set_fcr(red_calo_hit.get_fcr());
      udd_calo_hit.set_lt_trigger_counter(red_calo_hit.get_lt_trigger_counter());
      udd_calo_hit.set_lt_time_counter(red_calo_hit.get_lt_time_counter());
      udd_calo_hit.set_fwmeas_baseline(red_calo_hit.get_fwmeas_baseline());
      udd_calo_hit.set_fwmeas_peak_amplitude(red_calo_hit.get_fwmeas_peak_amplitude());
      udd_calo_hit.set_fwmeas_peak_cell(red_calo_hit.get_fwmeas_peak_cell());
      udd_calo_hit.set_fwmeas_charge(red_calo_hit.get_fwmeas_charge());
      udd_calo_hit.set_fwmeas_rising_cell(red_calo_hit.get_fwmeas_rising_cell());
      udd_calo_hit.set_fwmeas_falling_cell(red_calo_hit.get_fwmeas_falling_cell());
      snemo::datamodel::calorimeter_digitized_hit::rtd_origin the_rtd_origin(red_calo_hit.get_origin().get_hit_number(),
                                                                             red_calo_hit.get_origin().get_trigger_id());
      udd_calo_hit.set_origin(the_rtd_origin);

    } // end of for ihit

  // sort calo hit by om num
  auto & udd_calo_hits = UDD.grab_calorimeter_hits();

  std::sort(udd_calo_hits.begin(), udd_calo_hits.end(),
	    [](const auto & h1, const auto & h2) {return (snemo::datamodel::om_num(h1->get_geom_id()) < snemo::datamodel::om_num(h2->get_geom_id()));});

  // correct hit id values
  for (std::size_t ihit = 0; ihit < udd_calo_hits.size(); ihit++)
    udd_calo_hits[ihit]->set_hit_id(ihit);


  // Scan and copy RED tracker digitized hit into UDD calo digitized hit:
  for (std::size_t ihit = 0; ihit < red_tracker_hits.size(); ihit++)
    {
      // std::clog << "DEBUG do_red_to_udd_conversion : Tracker hit #" << ihit << std::endl;
      snfee::data::tracker_digitized_hit red_tracker_hit = red_tracker_hits[ihit];
      snemo::datamodel::tracker_digitized_hit & udd_tracker_hit = UDD.add_tracker_hit();
      udd_tracker_hit.set_geom_id(red_tracker_hit.get_geom_id());
      udd_tracker_hit.set_hit_id(red_tracker_hit.get_hit_id());

      // Do the loop on RED GG timestamps and convert them into UDD GG timestamps
	  const std::vector<snfee::data::tracker_digitized_hit::gg_times> gg_timestamps_v = red_tracker_hit.get_times();

      for (std::size_t iggtime = 0; iggtime < gg_timestamps_v.size(); iggtime++)
        {
          // std::clog << "DEBUG do_red_to_udd_conversion : Tracker hit #" << ihit << " Geiger Time #" << iggtime <<  std::endl;
          // Retrieve RED GG timestamps
	      const snfee::data::tracker_digitized_hit::gg_times & a_gg_timestamp = gg_timestamps_v[iggtime];

          // Create empty UDD GG timestamps
          snemo::datamodel::tracker_digitized_hit::gg_times & udd_gg_timestamp = udd_tracker_hit.add_times();

          // Fill UDD anode and cathode timestamps and RTD origin for backtracing
          snemo::datamodel::tracker_digitized_hit::rtd_origin anode_r0_rtd_origin(a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R0).get_hit_number(),
                                                                                  a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R0).get_trigger_id());
          udd_gg_timestamp.set_anode_origin(snemo::datamodel::tracker_digitized_hit::ANODE_R0,
                                            anode_r0_rtd_origin);
          udd_gg_timestamp.set_anode_time(snemo::datamodel::tracker_digitized_hit::ANODE_R0,
                                          a_gg_timestamp.get_anode_time(snfee::data::tracker_digitized_hit::ANODE_R0).get_ticks());

          snemo::datamodel::tracker_digitized_hit::rtd_origin anode_r1_rtd_origin(a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R1).get_hit_number(),
                                                                                  a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R1).get_trigger_id());
          udd_gg_timestamp.set_anode_origin(snemo::datamodel::tracker_digitized_hit::ANODE_R1,
                                            anode_r1_rtd_origin);
          udd_gg_timestamp.set_anode_time(snemo::datamodel::tracker_digitized_hit::ANODE_R1,
                                          a_gg_timestamp.get_anode_time(snfee::data::tracker_digitized_hit::ANODE_R1).get_ticks());

          snemo::datamodel::tracker_digitized_hit::rtd_origin anode_r2_rtd_origin(a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R2).get_hit_number(),
                                                                                  a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R2).get_trigger_id());
          udd_gg_timestamp.set_anode_origin(snemo::datamodel::tracker_digitized_hit::ANODE_R2,
                                            anode_r2_rtd_origin);
          udd_gg_timestamp.set_anode_time(snemo::datamodel::tracker_digitized_hit::ANODE_R2,
                                          a_gg_timestamp.get_anode_time(snfee::data::tracker_digitized_hit::ANODE_R2).get_ticks());

          snemo::datamodel::tracker_digitized_hit::rtd_origin anode_r3_rtd_origin(a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R3).get_hit_number(),
                                                                                  a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R3).get_trigger_id());
          udd_gg_timestamp.set_anode_origin(snemo::datamodel::tracker_digitized_hit::ANODE_R3,
                                            anode_r3_rtd_origin);
          udd_gg_timestamp.set_anode_time(snemo::datamodel::tracker_digitized_hit::ANODE_R3,
                                          a_gg_timestamp.get_anode_time(snfee::data::tracker_digitized_hit::ANODE_R3).get_ticks());

          snemo::datamodel::tracker_digitized_hit::rtd_origin anode_r4_rtd_origin(a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R4).get_hit_number(),
                                                                                  a_gg_timestamp.get_anode_origin(snfee::data::tracker_digitized_hit::ANODE_R4).get_trigger_id());
          udd_gg_timestamp.set_anode_origin(snemo::datamodel::tracker_digitized_hit::ANODE_R4,
                                            anode_r4_rtd_origin);
          udd_gg_timestamp.set_anode_time(snemo::datamodel::tracker_digitized_hit::ANODE_R4,
                                          a_gg_timestamp.get_anode_time(snfee::data::tracker_digitized_hit::ANODE_R4).get_ticks());

          snemo::datamodel::tracker_digitized_hit::rtd_origin bottom_cathode_rtd_origin(a_gg_timestamp.get_bottom_cathode_origin().get_hit_number(),
                                                                                        a_gg_timestamp.get_bottom_cathode_origin().get_trigger_id());
          udd_gg_timestamp.set_bottom_cathode_origin(bottom_cathode_rtd_origin);
          udd_gg_timestamp.set_bottom_cathode_time(a_gg_timestamp.get_bottom_cathode_time().get_ticks());


          snemo::datamodel::tracker_digitized_hit::rtd_origin top_cathode_rtd_origin(a_gg_timestamp.get_top_cathode_origin().get_hit_number(),
                                                                                     a_gg_timestamp.get_top_cathode_origin().get_trigger_id());
          udd_gg_timestamp.set_top_cathode_origin(top_cathode_rtd_origin);
          udd_gg_timestamp.set_top_cathode_time(a_gg_timestamp.get_top_cathode_time().get_ticks());


	    } // end of iggtime

    } // end for ihit

  // sort tracker hit by cell num
  auto & udd_tracker_hits = UDD.grab_tracker_hits();

  std::sort(udd_tracker_hits.begin(), udd_tracker_hits.end(),
	    [](const auto & h1, const auto & h2) {return (snemo::datamodel::gg_num(h1->get_geom_id()) < snemo::datamodel::gg_num(h2->get_geom_id()));});

  // correct hit id values
  for (std::size_t ihit = 0; ihit < udd_tracker_hits.size(); ihit++)
    udd_tracker_hits[ihit]->set_hit_id(ihit);

  // red_.print_tree(std::clog);
  // EH.tree_dump(std::clog, "Event header('EH'): ");
  // UDD.tree_dump(std::clog, "Unified Digitized Data('UDD'): ");

  return;
}
