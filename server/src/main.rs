#![feature(proc_macro_hygiene, decl_macro)]

#[macro_use]
extern crate rocket;
extern crate dotenv;
extern crate yaml_rust;

use dotenv::dotenv;
use rocket::State;
use std::env;
use std::process::Command;
use std::{fs::File, io::Read};
use yaml_rust::{yaml, YamlLoader};

#[post("/sensor/<id>")]
fn sensor(scripts_directory: State<String>, doc: State<yaml::Yaml>, id: String) -> String {
    for sensor in doc["sensors"].as_vec().unwrap() {
        if sensor["id"].as_str().unwrap() == id {
            for script_filename in sensor["scripts"].as_vec().unwrap() {
                let script_path = format!(
                    "{}/{}",
                    scripts_directory.as_str(),
                    script_filename.as_str().unwrap()
                );

                let output = run_script(script_path);
                let output = String::from_utf8_lossy(&output.stdout);

                print!("{} output: {}", script_filename.as_str().unwrap(), output);
            }
        }
    }

    format!("Sensor: {}", id)
}

fn main() {
    // Initialize environment variables
    dotenv().ok();

    let scripts_directory_result = env::var("SCRIPTS_DIRECTORY");
    let scripts_directory = match scripts_directory_result {
        Ok(v) => v,
        // Fall back to curent directory
        Err(_e) => String::from(".")
    };
    let yaml_path_result = env::var("YAML_PATH");
    let yaml_path = match yaml_path_result {
        Ok(v) => v,
        // Fall back to example file
        Err(_e) => String::from("./sensormap.example.yml")
    };

    let mut yaml_file = File::open(yaml_path).expect("No file with given name");
    let mut yaml_str = String::new();
    yaml_file
        .read_to_string(&mut yaml_str)
        .expect("Error while reading file");

    let docs_result = YamlLoader::load_from_str(&yaml_str);
    let mut docs = match docs_result {
        Ok(v) => v,
        Err(e) => panic!("Error while converting string to YAML: {:?}", e),
    };

    // Get first document as configuration YAML
    let doc = docs.remove(0);

    rocket::ignite()
        //Pass necessary data to the routes
        .manage(scripts_directory)
        .manage(doc)
        .mount("/api/v1", routes![sensor])
        .launch();
}

// Executes command as sh argument and returns output
fn run_script(path: String) -> std::process::Output {
    Command::new("sh")
        .arg("-c")
        .arg(path)
        .output()
        .expect("failed to execute command")
}
