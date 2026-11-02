import { useEffect, useMemo, useRef, type RefObject } from "react";
import { Canvas, useFrame, useThree } from "@react-three/fiber";
import { OrbitControls, PerspectiveCamera } from "@react-three/drei";
import type { OrbitControls as OrbitControlsImpl } from "three-stdlib";
import * as THREE from "three";
import { ALTITUDE_LIMIT_METERS, ALTITUDE_MARKS_METERS } from "../simulation/flightEnvelope";

interface DroneSceneProps {
  roll: number;
  pitch: number;
  heading: number;
  altitude: number;
  running: boolean;
  cameraMode: "follow" | "free";
  resetViewKey: number;
}

interface FollowControls {
  target: THREE.Vector3;
  update: () => void;
}

function dampAngle(current: number, target: number, smoothing: number, delta: number) {
  const difference = THREE.MathUtils.euclideanModulo(target - current + Math.PI, Math.PI * 2) - Math.PI;
  return current + difference * (1 - Math.exp(-smoothing * delta));
}

function Arm({ from, to, front }: { from: THREE.Vector3; to: THREE.Vector3; front: boolean }) {
  const direction = useMemo(() => to.clone().sub(from), [from, to]);
  const midpoint = useMemo(() => from.clone().add(to).multiplyScalar(0.5), [from, to]);
  return (
    <mesh position={midpoint} quaternion={new THREE.Quaternion().setFromUnitVectors(
      new THREE.Vector3(0, 1, 0), direction.clone().normalize(),
    )}>
      <cylinderGeometry args={[front ? 0.055 : 0.047, front ? 0.07 : 0.05, direction.length(), 8]} />
      <meshStandardMaterial color={front ? "#d8773f" : "#59646b"} metalness={0.62} roughness={0.36} />
    </mesh>
  );
}

function DroneModel({ roll, pitch, heading, altitude, running, cameraMode, controls }: DroneSceneProps & { controls: RefObject<FollowControls | null> }) {
  const aircraft = useRef<THREE.Group>(null);
  const rotors = useRef<Array<THREE.Group | null>>([]);
  const previousFollowPosition = useRef(new THREE.Vector3());
  const camera = useThree((state) => state.camera);
  const target = useMemo(() => new THREE.Euler(
    THREE.MathUtils.degToRad(pitch),
    THREE.MathUtils.degToRad(heading),
    -THREE.MathUtils.degToRad(roll),
    "YXZ",
  ), [heading, pitch, roll]);

  useFrame((_, delta) => {
    if (!aircraft.current) return;
    if (running) {
      aircraft.current.rotation.x = THREE.MathUtils.damp(aircraft.current.rotation.x, target.x, 9, delta);
      aircraft.current.rotation.y = dampAngle(aircraft.current.rotation.y, target.y, 9, delta);
      aircraft.current.rotation.z = THREE.MathUtils.damp(aircraft.current.rotation.z, target.z, 9, delta);
      aircraft.current.position.y = THREE.MathUtils.damp(aircraft.current.position.y, altitude, 3.5, delta);
    }
    if (cameraMode === "follow" && controls.current) {
      const movement = aircraft.current.position.clone().sub(previousFollowPosition.current);
      camera.position.add(movement);
      controls.current.target.add(movement);
      controls.current.update();
      previousFollowPosition.current.copy(aircraft.current.position);
    } else {
      previousFollowPosition.current.copy(aircraft.current.position);
    }
    if (running) {
      rotors.current.forEach((rotor, index) => {
        if (rotor) rotor.rotation.y += delta * 56 * (index % 2 === 0 ? 1 : -1);
      });
    }
  });

  const arms = useMemo(() => [
    { from: new THREE.Vector3(0, 0, 0), to: new THREE.Vector3(-0.74, 0, 0.74), front: true },
    { from: new THREE.Vector3(0, 0, 0), to: new THREE.Vector3(0.74, 0, 0.74), front: true },
    { from: new THREE.Vector3(0, 0, 0), to: new THREE.Vector3(-0.74, 0, -0.74), front: false },
    { from: new THREE.Vector3(0, 0, 0), to: new THREE.Vector3(0.74, 0, -0.74), front: false },
  ], []);
  const rotorPositions = useMemo(() => [
    [-0.74, 0, 0.74], [0.74, 0, 0.74], [-0.74, 0, -0.74], [0.74, 0, -0.74],
  ] as const, []);

  return (
    <group ref={aircraft} position={[0, 0, 0]}>
      {arms.map((arm, index) => <Arm key={index} {...arm} />)}
      <mesh position={[0, 0, 0]} castShadow>
        <cylinderGeometry args={[0.32, 0.38, 0.2, 8]} />
        <meshStandardMaterial color="#303a40" metalness={0.72} roughness={0.28} />
      </mesh>
      <mesh position={[0, 0.12, -0.015]} castShadow>
        <boxGeometry args={[0.42, 0.11, 0.48]} />
        <meshStandardMaterial color="#1a2227" metalness={0.55} roughness={0.38} />
      </mesh>
      <mesh position={[0, 0.06, 0.28]} rotation={[Math.PI / 2, 0, 0]}>
        <coneGeometry args={[0.15, 0.27, 8]} />
        <meshStandardMaterial color="#ef8c4d" metalness={0.35} roughness={0.3} />
      </mesh>
      <mesh position={[0, -0.11, 0.43]}>
        <sphereGeometry args={[0.095, 20, 14]} />
        <meshStandardMaterial color="#151d22" metalness={0.75} roughness={0.16} />
      </mesh>
      <mesh position={[0, -0.12, 0.49]}>
        <sphereGeometry args={[0.047, 18, 12]} />
        <meshStandardMaterial color="#63d5dc" emissive="#267e83" emissiveIntensity={0.7} />
      </mesh>
      {rotorPositions.map(([x, y, z], index) => (
        <group key={index} position={[x, y, z]}>
          <mesh castShadow>
            <cylinderGeometry args={[0.115, 0.13, 0.16, 16]} />
            <meshStandardMaterial color="#727f85" metalness={0.82} roughness={0.24} />
          </mesh>
          <mesh position={[0, 0.1, 0]}>
            <cylinderGeometry args={[0.078, 0.078, 0.045, 16]} />
            <meshStandardMaterial color="#202a2e" metalness={0.7} roughness={0.25} />
          </mesh>
          <group ref={(node) => { rotors.current[index] = node; }} position={[0, 0.14, 0]}>
            <mesh rotation={[0, 0, Math.PI / 2]}>
              <capsuleGeometry args={[0.026, 0.67, 4, 8]} />
              <meshStandardMaterial color="#8f9a9e" transparent opacity={0.78} roughness={0.35} />
            </mesh>
            <mesh rotation={[0, 0, -Math.PI / 2]}>
              <capsuleGeometry args={[0.026, 0.67, 4, 8]} />
              <meshStandardMaterial color="#7f8b90" transparent opacity={0.68} roughness={0.4} />
            </mesh>
          </group>
          <mesh position={[0, 0.01, 0]} castShadow>
            <sphereGeometry args={[0.055, 12, 10]} />
            <meshStandardMaterial color={index < 2 ? "#f08c50" : "#57c9a0"} emissive={index < 2 ? "#a94320" : "#145c44"} emissiveIntensity={0.45} />
          </mesh>
        </group>
      ))}
      {[-1, 1].map((side) => (
        <group key={side}>
          <mesh position={[side * 0.3, -0.32, 0]} rotation={[0, 0, side * -0.12]}>
            <cylinderGeometry args={[0.018, 0.018, 0.5, 8]} />
            <meshStandardMaterial color="#626d72" metalness={0.75} roughness={0.3} />
          </mesh>
          <mesh position={[side * 0.3, -0.57, 0]}>
            <cylinderGeometry args={[0.025, 0.025, 0.28, 8]} />
            <meshStandardMaterial color="#7a8589" metalness={0.68} roughness={0.34} />
          </mesh>
        </group>
      ))}
    </group>
  );
}

function Environment() {
  return (
    <>
      <gridHelper args={[20, 40, "#53666a", "#29383c"]} position={[0, -ALTITUDE_LIMIT_METERS, 0]} />
      <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, -ALTITUDE_LIMIT_METERS - 0.04, 0]} receiveShadow>
        <planeGeometry args={[200, 200]} />
        <meshStandardMaterial color="#131a1c" roughness={0.94} />
      </mesh>
      <axesHelper args={[1.1]} position={[-3.4, -ALTITUDE_LIMIT_METERS + 0.02, 0]} />
      <mesh position={[-2.8, 0, 0]}>
        <cylinderGeometry args={[0.008, 0.008, ALTITUDE_LIMIT_METERS * 2, 8]} />
        <meshStandardMaterial color="#627477" />
      </mesh>
      {ALTITUDE_MARKS_METERS.map((height) => (
        <group key={height} position={[-2.8, height, 0]}>
          <mesh position={[0, 0, 0]}>
            <boxGeometry args={[0.16, 0.012, 0.012]} />
            <meshBasicMaterial color={height === 0 ? "#65d3d5" : "#738184"} />
          </mesh>
        </group>
      ))}
      <mesh position={[0, 0, 0]} rotation={[Math.PI / 2, 0, 0]}>
        <ringGeometry args={[1.24, 1.255, 64]} />
        <meshBasicMaterial color="#304246" side={THREE.DoubleSide} transparent opacity={0.6} />
      </mesh>
      <mesh position={[0, -ALTITUDE_LIMIT_METERS, 0]} rotation={[-Math.PI / 2, 0, 0]} receiveShadow>
        <circleGeometry args={[0.5, 32]} />
        <meshBasicMaterial color="#dce8df" transparent opacity={0.07} />
      </mesh>
    </>
  );
}

function CameraReset({ resetViewKey, targetY, controls }: { resetViewKey: number; targetY: number; controls: RefObject<FollowControls | null> }) {
  const camera = useThree((state) => state.camera);
  const targetYRef = useRef(targetY);
  targetYRef.current = targetY;
  useEffect(() => {
    if (resetViewKey === 0) return;
    const currentTargetY = targetYRef.current;
    camera.position.set(5.2, 3.3 + currentTargetY, 6.2);
    controls.current?.target.set(0, currentTargetY, 0);
    controls.current?.update();
  }, [camera, controls, resetViewKey]);
  return null;
}

export default function DroneScene(props: DroneSceneProps) {
  const controls = useRef<OrbitControlsImpl & FollowControls>(null);
  return (
    <Canvas shadows dpr={[1, 1.75]} gl={{ antialias: true, alpha: false }}>
      <color attach="background" args={["#111719"]} />
      <PerspectiveCamera makeDefault position={[5.2, 3.3, 6.2]} fov={43} />
      <OrbitControls ref={controls} target={[0, 0, 0]} enablePan={props.cameraMode === "free"} enableDamping dampingFactor={0.08} minDistance={3.5} maxDistance={12} maxPolarAngle={Math.PI * 0.48} />
      <CameraReset
        resetViewKey={props.resetViewKey}
        targetY={props.cameraMode === "follow" ? props.altitude : 0}
        controls={controls}
      />
      <ambientLight intensity={1.05} />
      <directionalLight position={[4, 8, 3]} intensity={2.2} castShadow shadow-mapSize={[1024, 1024]} shadow-radius={5} shadow-bias={-0.00015} />
      <pointLight position={[-4, 2, -3]} intensity={35} color="#5ac6c8" />
      <Environment />
      <DroneModel {...props} controls={controls} />
    </Canvas>
  );
}
